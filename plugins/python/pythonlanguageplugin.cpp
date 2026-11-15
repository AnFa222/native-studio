#include "pythonlanguageplugin.h"

#include "pythonhighlighter.h"

#include <QFileInfo>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {
struct Delimiter
{
    QChar character;
    int line;
    int column;
};

bool delimitersMatch(QChar opening, QChar closing)
{
    return (opening == QLatin1Char('(') && closing == QLatin1Char(')'))
        || (opening == QLatin1Char('[') && closing == QLatin1Char(']'))
        || (opening == QLatin1Char('{') && closing == QLatin1Char('}'));
}

CodeDiagnostic diagnostic(int line, int column, QString message,
                          DiagnosticSeverity severity = DiagnosticSeverity::Error)
{
    return {line, column, 1, severity, std::move(message)};
}

QString codeWithoutPythonStringsAndComments(const QString &text)
{
    QString code = text;
    bool comment = false;
    bool quoted = false;
    bool triple = false;
    bool escaped = false;
    QChar quote;

    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar current = text.at(index);
        const QChar next = index + 1 < text.size() ? text.at(index + 1) : QChar{};
        const QChar afterNext = index + 2 < text.size() ? text.at(index + 2) : QChar{};

        if (comment) {
            if (current == QLatin1Char('\n')) {
                comment = false;
            } else {
                code[index] = QLatin1Char(' ');
            }
            continue;
        }

        if (quoted) {
            if (current == QLatin1Char('\n')) {
                if (!triple) quoted = false;
                escaped = false;
                continue;
            }
            code[index] = QLatin1Char(' ');
            if (escaped) {
                escaped = false;
                continue;
            }
            if (current == QLatin1Char('\\')) {
                escaped = true;
                continue;
            }
            if (triple && current == quote && next == quote && afterNext == quote) {
                code[index + 1] = QLatin1Char(' ');
                code[index + 2] = QLatin1Char(' ');
                index += 2;
                quoted = false;
            } else if (!triple && current == quote) {
                quoted = false;
            }
            continue;
        }

        if (current == QLatin1Char('#')) {
            code[index] = QLatin1Char(' ');
            comment = true;
            continue;
        }
        if (current != QLatin1Char('\'') && current != QLatin1Char('"')) continue;

        qsizetype prefixStart = index;
        while (prefixStart > 0 && text.at(prefixStart - 1).isLetter()) --prefixStart;
        const QString prefix = text.mid(prefixStart, index - prefixStart).toLower();
        static const QSet<QString> stringPrefixes{
            QStringLiteral("r"), QStringLiteral("u"), QStringLiteral("b"),
            QStringLiteral("f"), QStringLiteral("br"), QStringLiteral("rb"),
            QStringLiteral("fr"), QStringLiteral("rf")
        };
        if (stringPrefixes.contains(prefix)) {
            for (qsizetype prefixIndex = prefixStart; prefixIndex < index; ++prefixIndex)
                code[prefixIndex] = QLatin1Char(' ');
        }
        quote = current;
        triple = next == current && afterNext == current;
        code[index] = QLatin1Char(' ');
        if (triple) {
            code[index + 1] = QLatin1Char(' ');
            code[index + 2] = QLatin1Char(' ');
            index += 2;
        }
        quoted = true;
        escaped = false;
    }
    return code;
}

int indentationWidth(const QString &line, int *characterCount = nullptr)
{
    int width = 0;
    int count = 0;
    while (count < line.size()) {
        if (line.at(count) == QLatin1Char(' ')) {
            ++width;
        } else if (line.at(count) == QLatin1Char('\t')) {
            width += 4 - (width % 4);
        } else {
            break;
        }
        ++count;
    }
    if (characterCount) *characterCount = count;
    return width;
}

int topLevelColon(const QString &line)
{
    int depth = 0;
    for (int index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (character == QLatin1Char('(') || character == QLatin1Char('[')
            || character == QLatin1Char('{')) {
            ++depth;
        } else if (character == QLatin1Char(')') || character == QLatin1Char(']')
                   || character == QLatin1Char('}')) {
            depth = qMax(0, depth - 1);
        } else if (character == QLatin1Char(':') && depth == 0) {
            return index;
        }
    }
    return -1;
}
}

PythonLanguagePlugin::~PythonLanguagePlugin()
{
    shutdown();
}

void PythonLanguagePlugin::shutdown()
{
    if (!m_analyzerProcess || m_analyzerProcess->state() == QProcess::NotRunning) return;
    m_analyzerProcess->closeWriteChannel();
    m_analyzerProcess->terminate();
    if (!m_analyzerProcess->waitForFinished(150)) {
        m_analyzerProcess->kill();
        m_analyzerProcess->waitForFinished(150);
    }
}

bool PythonLanguagePlugin::ensureAnalyzerRunning() const
{
    if (m_analyzerUnavailable) return false;
    if (m_analyzerProcess && m_analyzerProcess->state() == QProcess::Running) return true;

    QFile scriptFile(QStringLiteral(":/native_studio/python/analyzer.py"));
    if (!scriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_analyzerUnavailable = true;
        return false;
    }
    const QString script = QString::fromUtf8(scriptFile.readAll());
    if (!m_analyzerProcess)
        m_analyzerProcess = new QProcess(const_cast<PythonLanguagePlugin *>(this));

#ifdef Q_OS_WIN
    const QStringList candidates{QStringLiteral("python"), QStringLiteral("python3"),
                                 QStringLiteral("py")};
#else
    const QStringList candidates{QStringLiteral("python3"), QStringLiteral("python")};
#endif
    for (const QString &candidate : candidates) {
        const QString executable = QStandardPaths::findExecutable(candidate);
        if (executable.isEmpty()) continue;
        QStringList arguments;
        if (QFileInfo(executable).completeBaseName().compare(QStringLiteral("py"),
                                                             Qt::CaseInsensitive) == 0)
            arguments.push_back(QStringLiteral("-3"));
        arguments << QStringLiteral("-u") << QStringLiteral("-c") << script;
        m_analyzerProcess->setProgram(executable);
        m_analyzerProcess->setArguments(arguments);
        m_analyzerProcess->setProcessChannelMode(QProcess::SeparateChannels);
        m_analyzerProcess->start();
        if (m_analyzerProcess->waitForStarted(700)) return true;
        m_analyzerProcess->close();
    }
    m_analyzerUnavailable = true;
    return false;
}

std::optional<QList<CodeDiagnostic>>
PythonLanguagePlugin::analyzerDiagnostics(const QString &documentText) const
{
    if (!ensureAnalyzerRunning()) return std::nullopt;
    const auto analyzerFailure = [this]() -> std::optional<QList<CodeDiagnostic>> {
        m_analyzerUnavailable = true;
        const_cast<PythonLanguagePlugin *>(this)->shutdown();
        return std::nullopt;
    };

    QJsonObject request;
    request.insert(QStringLiteral("source"), documentText);
    QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (m_analyzerProcess->write(payload) != payload.size()
        || !m_analyzerProcess->waitForBytesWritten(300)) return analyzerFailure();

    QElapsedTimer timer;
    timer.start();
    while (!m_analyzerProcess->canReadLine() && timer.elapsed() < 1200) {
        if (m_analyzerProcess->state() == QProcess::NotRunning) break;
        m_analyzerProcess->waitForReadyRead(50);
    }
    if (!m_analyzerProcess->canReadLine()) return analyzerFailure();

    QJsonParseError parseError;
    const QJsonDocument response = QJsonDocument::fromJson(
        m_analyzerProcess->readLine().trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !response.isArray())
        return analyzerFailure();

    QList<CodeDiagnostic> diagnostics;
    const QJsonArray entries = response.array();
    diagnostics.reserve(entries.size());
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        const QString message = entry.value(QStringLiteral("message")).toString();
        if (message.startsWith(QStringLiteral("Python analyzer failed:"))) return analyzerFailure();
        diagnostics.push_back({
            entry.value(QStringLiteral("line")).toInt(),
            entry.value(QStringLiteral("column")).toInt(),
            qMax(1, entry.value(QStringLiteral("length")).toInt(1)),
            entry.value(QStringLiteral("severity")).toString() == QStringLiteral("error")
                ? DiagnosticSeverity::Error : DiagnosticSeverity::Warning,
            message
        });
    }
    return diagnostics;
}

QString PythonLanguagePlugin::languageName() const
{
    return QStringLiteral("Python");
}

QStringList PythonLanguagePlugin::fileExtensions() const
{
    return {QStringLiteral("py"), QStringLiteral("pyw"), QStringLiteral("pyi")};
}

QSyntaxHighlighter *PythonLanguagePlugin::createHighlighter(QTextDocument *document)
{
    return new PythonHighlighter(document);
}

RunConfiguration PythonLanguagePlugin::runConfiguration(const QString &filePath) const
{
    RunConfiguration configuration;
#ifdef Q_OS_WIN
    configuration.program = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (configuration.program.isEmpty()) {
        configuration.program = QStringLiteral("py");
        configuration.arguments = {QStringLiteral("-3"), QFileInfo(filePath).absoluteFilePath()};
    } else {
        configuration.arguments = {QFileInfo(filePath).absoluteFilePath()};
    }
#else
    configuration.program = QStringLiteral("python3");
    configuration.arguments = {QFileInfo(filePath).absoluteFilePath()};
#endif
    configuration.workingDirectory = QFileInfo(filePath).absolutePath();
    return configuration;
}

QStringList PythonLanguagePlugin::completionItems(const QString &documentText) const
{
    QSet<QString> items = {
        QStringLiteral("and"), QStringLiteral("as"), QStringLiteral("assert"),
        QStringLiteral("async"), QStringLiteral("await"), QStringLiteral("break"),
        QStringLiteral("class"), QStringLiteral("continue"), QStringLiteral("def"),
        QStringLiteral("del"), QStringLiteral("elif"), QStringLiteral("else"),
        QStringLiteral("except"), QStringLiteral("False"), QStringLiteral("finally"),
        QStringLiteral("for"), QStringLiteral("from"), QStringLiteral("global"),
        QStringLiteral("if"), QStringLiteral("import"), QStringLiteral("in"),
        QStringLiteral("is"), QStringLiteral("lambda"), QStringLiteral("match"),
        QStringLiteral("None"), QStringLiteral("nonlocal"), QStringLiteral("not"),
        QStringLiteral("or"), QStringLiteral("pass"), QStringLiteral("raise"),
        QStringLiteral("return"), QStringLiteral("True"), QStringLiteral("try"),
        QStringLiteral("while"), QStringLiteral("with"), QStringLiteral("yield"),
        QStringLiteral("print"), QStringLiteral("len"), QStringLiteral("range"),
        QStringLiteral("enumerate"), QStringLiteral("zip"), QStringLiteral("open"),
        QStringLiteral("input"), QStringLiteral("int"), QStringLiteral("float"),
        QStringLiteral("str"), QStringLiteral("list"), QStringLiteral("dict"),
        QStringLiteral("set"), QStringLiteral("tuple"), QStringLiteral("bool"),
        QStringLiteral("super"), QStringLiteral("property"), QStringLiteral("staticmethod"),
        QStringLiteral("classmethod"), QStringLiteral("isinstance"), QStringLiteral("Exception")
    };
    const QRegularExpression identifier(QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_]*\b)"));
    auto matches = identifier.globalMatch(codeWithoutPythonStringsAndComments(documentText));
    while (matches.hasNext()) items.insert(matches.next().captured());

    QStringList result(items.begin(), items.end());
    result.sort(Qt::CaseInsensitive);
    return result;
}

QList<CodeDiagnostic> PythonLanguagePlugin::diagnostics(const QString &documentText) const
{
    if (const auto diagnostics = analyzerDiagnostics(documentText)) return *diagnostics;

    QList<CodeDiagnostic> result;
    QList<Delimiter> delimiters;
    bool comment = false;
    bool quoted = false;
    bool triple = false;
    bool escaped = false;
    QChar quote;
    int constructLine = 0;
    int constructColumn = 0;
    int line = 0;
    int column = 0;

    for (qsizetype index = 0; index < documentText.size(); ++index) {
        const QChar current = documentText.at(index);
        const QChar next = index + 1 < documentText.size() ? documentText.at(index + 1) : QChar{};
        const QChar afterNext = index + 2 < documentText.size() ? documentText.at(index + 2) : QChar{};

        if (current == QLatin1Char('\n')) {
            comment = false;
            if (quoted && !triple && !escaped) {
                result.push_back(diagnostic(constructLine, constructColumn,
                                            QStringLiteral("Unterminated string literal")));
                quoted = false;
            }
            escaped = false;
            ++line;
            column = 0;
            continue;
        }
        if (comment) {
            ++column;
            continue;
        }
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (current == QLatin1Char('\\')) {
                escaped = true;
            } else if (triple && current == quote && next == quote && afterNext == quote) {
                quoted = false;
                triple = false;
                index += 2;
                column += 3;
                continue;
            } else if (!triple && current == quote) {
                quoted = false;
            }
            ++column;
            continue;
        }

        if (current == QLatin1Char('#')) {
            comment = true;
            ++column;
            continue;
        }
        if (current == QLatin1Char('"') || current == QLatin1Char('\'')) {
            quote = current;
            quoted = true;
            triple = next == current && afterNext == current;
            constructLine = line;
            constructColumn = column;
            if (triple) {
                index += 2;
                column += 3;
            } else {
                ++column;
            }
            continue;
        }
        if (current == QLatin1Char('(') || current == QLatin1Char('[')
            || current == QLatin1Char('{')) {
            delimiters.push_back({current, line, column});
        } else if (current == QLatin1Char(')') || current == QLatin1Char(']')
                   || current == QLatin1Char('}')) {
            if (delimiters.isEmpty()) {
                result.push_back(diagnostic(line, column,
                    QStringLiteral("Unmatched closing '%1'").arg(current)));
            } else {
                const auto opening = delimiters.takeLast();
                if (!delimitersMatch(opening.character, current)) {
                    result.push_back(diagnostic(line, column,
                        QStringLiteral("Closing '%1' does not match '%2'")
                            .arg(current, opening.character)));
                }
            }
        }
        ++column;
    }

    if (quoted)
        result.push_back(diagnostic(constructLine, constructColumn,
            triple ? QStringLiteral("Unterminated triple-quoted string")
                   : QStringLiteral("Unterminated string literal")));
    for (const auto &opening : delimiters)
        result.push_back(diagnostic(opening.line, opening.column,
            QStringLiteral("Unclosed '%1'").arg(opening.character)));

    const QRegularExpression suiteHeader(QStringLiteral(
        R"(^\s*(async\s+(?:def|for|with)|def|class|if|elif|else|for|while|try|except|finally|with|match|case)\b)"));
    const auto lines = documentText.split(QLatin1Char('\n'));
    const auto codeLines = codeWithoutPythonStringsAndComments(documentText).split(QLatin1Char('\n'));
    QList<int> indentationLevels{0};
    bool expectsIndentedBlock = false;
    int suiteIndentation = 0;
    int suiteLine = -1;
    int continuationDepth = 0;
    bool explicitContinuation = false;
    QString pendingSuiteKind;
    int pendingSuiteIndentation = 0;
    int pendingSuiteLine = -1;
    for (int lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        const QString &sourceLine = lines.at(lineNumber);
        const QString syntaxLine = codeLines.value(lineNumber);
        int indentationLength = 0;
        const int indentation = indentationWidth(sourceLine, &indentationLength);
        const QString indentationText = sourceLine.left(indentationLength);
        if (indentationText.contains(QLatin1Char(' '))
            && indentationText.contains(QLatin1Char('\t'))) {
            result.push_back(diagnostic(lineNumber, 0,
                QStringLiteral("Indentation mixes tabs and spaces"), DiagnosticSeverity::Warning));
        }

        const QString code = syntaxLine.trimmed();
        const int depthBeforeLine = continuationDepth;
        const bool continuedFromPreviousLine = explicitContinuation;
        for (const QChar character : syntaxLine) {
            if (character == QLatin1Char('(') || character == QLatin1Char('[')
                || character == QLatin1Char('{')) ++continuationDepth;
            else if (character == QLatin1Char(')') || character == QLatin1Char(']')
                     || character == QLatin1Char('}')) continuationDepth = qMax(0, continuationDepth - 1);
        }
        explicitContinuation = code.endsWith(QLatin1Char('\\'));
        if (code.isEmpty()) continue;

        const auto possibleHeader = suiteHeader.match(code);
        if (depthBeforeLine == 0 && !continuedFromPreviousLine
            && (continuationDepth > 0 || explicitContinuation)
            && possibleHeader.hasMatch()) {
            pendingSuiteKind = possibleHeader.captured(1).simplified();
            pendingSuiteIndentation = indentation;
            pendingSuiteLine = lineNumber;
        }
        if (depthBeforeLine > 0 || continuedFromPreviousLine) {
            if (continuationDepth == 0 && !explicitContinuation && !pendingSuiteKind.isEmpty()) {
                const int colon = topLevelColon(code);
                if (colon < 0) {
                    result.push_back(diagnostic(pendingSuiteLine, 0, QStringLiteral("Expected ':'")));
                } else if (code.mid(colon + 1).trimmed().isEmpty()) {
                    expectsIndentedBlock = true;
                    suiteIndentation = pendingSuiteIndentation;
                    suiteLine = pendingSuiteLine;
                }
                pendingSuiteKind.clear();
            }
            continue;
        }
        if (continuationDepth > 0 || explicitContinuation) continue;

        if (expectsIndentedBlock) {
            if (indentation <= suiteIndentation) {
                result.push_back(diagnostic(suiteLine, 0,
                    QStringLiteral("Expected an indented block")));
            } else if (indentation > indentationLevels.constLast()) {
                indentationLevels.push_back(indentation);
            }
            expectsIndentedBlock = false;
        } else {
            while (indentationLevels.size() > 1
                   && indentation < indentationLevels.constLast()) indentationLevels.removeLast();
            if (indentation > indentationLevels.constLast()) {
                result.push_back(diagnostic(lineNumber, 0, QStringLiteral("Unexpected indentation")));
            } else if (indentation != indentationLevels.constLast()) {
                result.push_back(diagnostic(lineNumber, 0, QStringLiteral("Inconsistent indentation")));
            }
        }

        const auto header = possibleHeader;
        if (!header.hasMatch()) continue;
        const int colon = topLevelColon(code);
        if (colon < 0) {
            result.push_back(diagnostic(lineNumber, qMax(0, sourceLine.size() - 1),
                                        QStringLiteral("Expected ':'")));
            continue;
        }

        const QString kind = header.captured(1).simplified();
        const QString beforeColon = code.left(colon).trimmed();
        bool malformed = false;
        if (kind == QStringLiteral("def") || kind == QStringLiteral("async def")) {
            static const QRegularExpression definition(QStringLiteral(
                R"(^(?:async\s+)?def\s+[A-Za-z_][A-Za-z0-9_]*\s*\(.*\)\s*(?:->\s*.+)?$)"));
            malformed = !definition.match(beforeColon).hasMatch();
        } else if (kind == QStringLiteral("class")) {
            static const QRegularExpression classDefinition(QStringLiteral(
                R"(^class\s+[A-Za-z_][A-Za-z0-9_]*(?:\s*\(.*\))?$)"));
            malformed = !classDefinition.match(beforeColon).hasMatch();
        } else if (kind == QStringLiteral("for") || kind == QStringLiteral("async for")) {
            malformed = !beforeColon.contains(QRegularExpression(QStringLiteral(R"(\s+in\s+)")));
        } else if (kind == QStringLiteral("if") || kind == QStringLiteral("elif")
                   || kind == QStringLiteral("while") || kind == QStringLiteral("with")
                   || kind == QStringLiteral("async with") || kind == QStringLiteral("match")
                   || kind == QStringLiteral("case")) {
            malformed = beforeColon.mid(kind.size()).trimmed().isEmpty();
        }
        if (malformed) {
            result.push_back(diagnostic(lineNumber, indentationLength,
                QStringLiteral("Malformed %1 statement").arg(kind)));
        }

        if (code.mid(colon + 1).trimmed().isEmpty()) {
            expectsIndentedBlock = true;
            suiteIndentation = indentation;
            suiteLine = lineNumber;
        }
    }
    if (expectsIndentedBlock)
        result.push_back(diagnostic(suiteLine, 0, QStringLiteral("Expected an indented block")));
    return result;
}
