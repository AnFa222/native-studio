#include "pythonlanguageplugin.h"

#include "pythonhighlighter.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

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
    configuration.program = QStringLiteral("py");
    configuration.arguments = {QStringLiteral("-3"), QFileInfo(filePath).absoluteFilePath()};
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

    const QRegularExpression requiresColon(QStringLiteral(
        R"(^\s*(async\s+def|def|class|if|elif|else|for|while|try|except|finally|with|match|case)\b)"));
    const auto lines = documentText.split(QLatin1Char('\n'));
    for (int lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        const QString &sourceLine = lines.at(lineNumber);
        int indentationLength = 0;
        while (indentationLength < sourceLine.size()
               && (sourceLine.at(indentationLength) == QLatin1Char(' ')
                   || sourceLine.at(indentationLength) == QLatin1Char('\t'))) {
            ++indentationLength;
        }
        const QString indentation = sourceLine.left(indentationLength);
        if (indentation.contains(QLatin1Char(' ')) && indentation.contains(QLatin1Char('\t'))) {
            result.push_back(diagnostic(lineNumber, 0,
                QStringLiteral("Indentation mixes tabs and spaces"), DiagnosticSeverity::Warning));
        }
        const QString code = sourceLine.section(QLatin1Char('#'), 0, 0).trimmed();
        if (!code.isEmpty() && requiresColon.match(code).hasMatch()
            && !code.contains(QLatin1Char(':')) && !code.endsWith(QLatin1Char('\\'))) {
            result.push_back(diagnostic(lineNumber, qMax(0, sourceLine.size() - 1),
                                        QStringLiteral("Expected ':'")));
        }
    }
    return result;
}
