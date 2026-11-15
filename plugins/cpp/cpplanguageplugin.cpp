#include "cpplanguageplugin.h"

#include "cpphighlighter.h"

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

void blankCodeRange(QString &code, qsizetype first, qsizetype last)
{
    for (qsizetype index = first; index <= last && index < code.size(); ++index) {
        if (code.at(index) != QLatin1Char('\n')) code[index] = QLatin1Char(' ');
    }
}

QString codeWithoutCppStringsAndComments(const QString &text)
{
    QString code = text;
    bool lineComment = false;
    bool blockComment = false;
    bool quoted = false;
    bool escaped = false;
    QChar quote;

    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar current = text.at(index);
        const QChar next = index + 1 < text.size() ? text.at(index + 1) : QChar{};

        if (lineComment) {
            if (current == QLatin1Char('\n')) {
                lineComment = false;
            } else {
                code[index] = QLatin1Char(' ');
            }
            continue;
        }
        if (blockComment) {
            code[index] = current == QLatin1Char('\n') ? current : QLatin1Char(' ');
            if (current == QLatin1Char('*') && next == QLatin1Char('/')) {
                code[index + 1] = QLatin1Char(' ');
                ++index;
                blockComment = false;
            }
            continue;
        }
        if (quoted) {
            code[index] = current == QLatin1Char('\n') ? current : QLatin1Char(' ');
            if (escaped) {
                escaped = false;
            } else if (current == QLatin1Char('\\')) {
                escaped = true;
            } else if (current == quote || current == QLatin1Char('\n')) {
                quoted = false;
            }
            continue;
        }

        if (current == QLatin1Char('/') && next == QLatin1Char('/')) {
            code[index] = code[index + 1] = QLatin1Char(' ');
            ++index;
            lineComment = true;
            continue;
        }
        if (current == QLatin1Char('/') && next == QLatin1Char('*')) {
            code[index] = code[index + 1] = QLatin1Char(' ');
            ++index;
            blockComment = true;
            continue;
        }
        if (current != QLatin1Char('"') && current != QLatin1Char('\'')) continue;

        qsizetype prefixStart = index;
        while (prefixStart > 0 && (text.at(prefixStart - 1).isLetterOrNumber()
               || text.at(prefixStart - 1) == QLatin1Char('_'))) --prefixStart;
        const QString prefix = text.mid(prefixStart, index - prefixStart);
        static const QSet<QString> literalPrefixes{
            QStringLiteral("u8"), QStringLiteral("u"), QStringLiteral("U"),
            QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("u8R"),
            QStringLiteral("uR"), QStringLiteral("UR"), QStringLiteral("LR")
        };
        const bool recognizedPrefix = literalPrefixes.contains(prefix);
        if (recognizedPrefix) blankCodeRange(code, prefixStart, index - 1);

        if (current == QLatin1Char('"') && recognizedPrefix && prefix.endsWith(QLatin1Char('R'))) {
            const qsizetype openParenthesis = text.indexOf(QLatin1Char('('), index + 1);
            if (openParenthesis >= 0 && openParenthesis - index <= 17) {
                const QString delimiter = text.mid(index + 1, openParenthesis - index - 1);
                const QString terminator = QLatin1Char(')') + delimiter + QLatin1Char('"');
                const qsizetype terminatorStart = text.indexOf(terminator, openParenthesis + 1);
                const qsizetype literalEnd = terminatorStart >= 0
                    ? terminatorStart + terminator.size() - 1 : text.size() - 1;
                blankCodeRange(code, index, literalEnd);
                index = literalEnd;
                continue;
            }
        }

        code[index] = QLatin1Char(' ');
        quoted = true;
        escaped = false;
        quote = current;
    }
    return code;
}
}

QString CppLanguagePlugin::languageName() const
{
    return QStringLiteral("C++");
}

QStringList CppLanguagePlugin::fileExtensions() const
{
    return {QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
            QStringLiteral("cxx"), QStringLiteral("h"), QStringLiteral("hh"),
            QStringLiteral("hpp"), QStringLiteral("hxx"), QStringLiteral("ixx"),
            QStringLiteral("cppm")};
}

QSyntaxHighlighter *CppLanguagePlugin::createHighlighter(QTextDocument *document)
{
    return new CppHighlighter(document);
}

int CppLanguagePlugin::indentationDeltaAfterLine(const QString &lineText) const
{
    return codeWithoutCppStringsAndComments(lineText).trimmed().endsWith(QLatin1Char('{')) ? 1 : 0;
}

QList<CodeSymbol> CppLanguagePlugin::documentSymbols(const QString &documentText) const
{
    QList<CodeSymbol> symbols;
    const QStringList lines = codeWithoutCppStringsAndComments(documentText).split(QLatin1Char('\n'));
    const QRegularExpression typePattern(
        QStringLiteral(R"(^\s*(class|struct|enum|namespace)\s+([A-Za-z_][A-Za-z0-9_]*))"));
    const QRegularExpression functionPattern(
        QStringLiteral(R"(^\s*(?:[\w:<>,~*&]+\s+)+([A-Za-z_~][A-Za-z0-9_:~]*)\s*\([^;]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:\{|$))"));
    for (int line = 0; line < lines.size(); ++line) {
        const QString &text = lines.at(line);
        auto match = typePattern.match(text);
        if (match.hasMatch()) {
            symbols.push_back({match.captured(2), match.captured(1), line,
                               static_cast<int>(match.capturedStart(2)), 0});
            continue;
        }
        match = functionPattern.match(text);
        if (match.hasMatch())
            symbols.push_back({match.captured(1), QStringLiteral("function"), line,
                               static_cast<int>(match.capturedStart(1)), 0});
    }
    return symbols;
}

QStringList CppLanguagePlugin::completionItems(const QString &documentText) const
{
    QSet<QString> items = {
        QStringLiteral("alignas"), QStringLiteral("alignof"), QStringLiteral("auto"),
        QStringLiteral("bool"), QStringLiteral("break"), QStringLiteral("case"),
        QStringLiteral("catch"), QStringLiteral("char"), QStringLiteral("class"),
        QStringLiteral("concept"), QStringLiteral("const"), QStringLiteral("consteval"),
        QStringLiteral("constexpr"), QStringLiteral("constinit"), QStringLiteral("continue"),
        QStringLiteral("co_await"), QStringLiteral("co_return"), QStringLiteral("co_yield"),
        QStringLiteral("decltype"), QStringLiteral("default"), QStringLiteral("delete"),
        QStringLiteral("do"), QStringLiteral("double"), QStringLiteral("else"),
        QStringLiteral("enum"), QStringLiteral("explicit"), QStringLiteral("export"),
        QStringLiteral("extern"), QStringLiteral("false"), QStringLiteral("float"),
        QStringLiteral("for"), QStringLiteral("friend"), QStringLiteral("if"),
        QStringLiteral("inline"), QStringLiteral("int"), QStringLiteral("long"),
        QStringLiteral("namespace"), QStringLiteral("new"), QStringLiteral("noexcept"),
        QStringLiteral("nullptr"), QStringLiteral("operator"), QStringLiteral("override"),
        QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("public"),
        QStringLiteral("requires"), QStringLiteral("return"), QStringLiteral("short"),
        QStringLiteral("signed"), QStringLiteral("sizeof"), QStringLiteral("static"),
        QStringLiteral("static_assert"), QStringLiteral("struct"), QStringLiteral("switch"),
        QStringLiteral("template"), QStringLiteral("this"), QStringLiteral("throw"),
        QStringLiteral("true"), QStringLiteral("try"), QStringLiteral("typedef"),
        QStringLiteral("typename"), QStringLiteral("union"), QStringLiteral("unsigned"),
        QStringLiteral("using"), QStringLiteral("virtual"), QStringLiteral("void"),
        QStringLiteral("volatile"), QStringLiteral("while"), QStringLiteral("std"),
        QStringLiteral("string"), QStringLiteral("vector"), QStringLiteral("array"),
        QStringLiteral("map"), QStringLiteral("unordered_map"), QStringLiteral("unique_ptr"),
        QStringLiteral("shared_ptr"), QStringLiteral("make_unique"), QStringLiteral("make_shared"),
        QStringLiteral("cout"), QStringLiteral("cin"), QStringLiteral("cerr"),
        QStringLiteral("move"), QStringLiteral("forward")
    };
    const QRegularExpression identifier(QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_]*\b)"));
    auto matches = identifier.globalMatch(codeWithoutCppStringsAndComments(documentText));
    while (matches.hasNext()) items.insert(matches.next().captured());

    QStringList result(items.begin(), items.end());
    result.sort(Qt::CaseInsensitive);
    return result;
}

QList<CodeDiagnostic> CppLanguagePlugin::diagnostics(const QString &documentText) const
{
    QList<CodeDiagnostic> result;
    QList<Delimiter> delimiters;
    const QString codeOnly = codeWithoutCppStringsAndComments(documentText);
    bool lineComment = false;
    bool blockComment = false;
    bool quoted = false;
    bool character = false;
    bool escaped = false;
    int constructLine = 0;
    int constructColumn = 0;
    int line = 0;
    int column = 0;

    for (qsizetype index = 0; index < documentText.size(); ++index) {
        const QChar current = documentText.at(index);
        const QChar next = index + 1 < documentText.size() ? documentText.at(index + 1) : QChar{};
        const QChar syntaxCharacter = codeOnly.at(index);

        if (current == QLatin1Char('\n')) {
            lineComment = false;
            if ((quoted || character) && !escaped) {
                result.push_back(diagnostic(constructLine, constructColumn,
                    quoted ? QStringLiteral("Unterminated string literal")
                           : QStringLiteral("Unterminated character literal")));
                quoted = false;
                character = false;
            }
            escaped = false;
            ++line;
            column = 0;
            continue;
        }

        if (lineComment) {
            ++column;
            continue;
        }
        if (blockComment) {
            if (current == QLatin1Char('*') && next == QLatin1Char('/')) {
                blockComment = false;
                ++index;
                column += 2;
            } else {
                ++column;
            }
            continue;
        }
        if (quoted || character) {
            if (escaped) {
                escaped = false;
            } else if (current == QLatin1Char('\\')) {
                escaped = true;
            } else if ((quoted && current == QLatin1Char('"'))
                       || (character && current == QLatin1Char('\''))) {
                quoted = false;
                character = false;
            }
            ++column;
            continue;
        }

        if (current == QLatin1Char('/') && next == QLatin1Char('/')) {
            lineComment = true;
            ++index;
            column += 2;
            continue;
        }
        if (current == QLatin1Char('/') && next == QLatin1Char('*')) {
            blockComment = true;
            constructLine = line;
            constructColumn = column;
            ++index;
            column += 2;
            continue;
        }
        if (current == QLatin1Char('"') || current == QLatin1Char('\'')) {
            quoted = current == QLatin1Char('"');
            character = !quoted;
            constructLine = line;
            constructColumn = column;
            ++column;
            continue;
        }
        if (syntaxCharacter == QLatin1Char('(') || syntaxCharacter == QLatin1Char('[')
            || syntaxCharacter == QLatin1Char('{')) {
            delimiters.push_back({syntaxCharacter, line, column});
        } else if (syntaxCharacter == QLatin1Char(')') || syntaxCharacter == QLatin1Char(']')
                   || syntaxCharacter == QLatin1Char('}')) {
            if (delimiters.isEmpty()) {
                result.push_back(diagnostic(line, column,
                    QStringLiteral("Unmatched closing '%1'").arg(syntaxCharacter)));
            } else {
                const auto opening = delimiters.takeLast();
                if (!delimitersMatch(opening.character, syntaxCharacter)) {
                    result.push_back(diagnostic(line, column,
                        QStringLiteral("Closing '%1' does not match '%2'")
                            .arg(syntaxCharacter, opening.character)));
                }
            }
        }
        ++column;
    }

    if (blockComment)
        result.push_back(diagnostic(constructLine, constructColumn,
                                    QStringLiteral("Unterminated block comment")));
    if (quoted || character)
        result.push_back(diagnostic(constructLine, constructColumn,
            quoted ? QStringLiteral("Unterminated string literal")
                   : QStringLiteral("Unterminated character literal")));
    for (const auto &opening : delimiters)
        result.push_back(diagnostic(opening.line, opening.column,
            QStringLiteral("Unclosed '%1'").arg(opening.character)));

    const auto lines = documentText.split(QLatin1Char('\n'));
    const auto syntaxLines = codeOnly.split(QLatin1Char('\n'));
    const QRegularExpression controlStatement(QStringLiteral(
        R"(^\s*(?:else\s+)?(if|for|while|switch|catch)\b(.*)$)"));
    const QRegularExpression definiteStatement(QStringLiteral(
        R"(^\s*(?:return|break|continue|throw|co_return|goto|delete|using\s+namespace|typedef)\b)"));
    const QRegularExpression declaration(QStringLiteral(
        R"(^\s*(?:(?:const|constexpr|constinit|static|extern|volatile|mutable|signed|unsigned|long|short|struct|class|typename)\s+)*(?:auto|bool|char|char8_t|char16_t|char32_t|double|float|int|void|wchar_t|[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*(?:\s*<.*>)?)\s+[*&\s]*[A-Za-z_][A-Za-z0-9_]*(?:\s*(?:=|\{|\().*)?$)"));
    const QRegularExpression assignment(QStringLiteral(
        R"(^\s*[A-Za-z_][A-Za-z0-9_]*(?:(?:\.|->|::)[A-Za-z_][A-Za-z0-9_]*|\s*\[[^\]]+\])*\s*(?:[+\-*/%&|^]?=|\+\+|--))"));
    const QRegularExpression callExpression(QStringLiteral(
        R"(^\s*[A-Za-z_][A-Za-z0-9_]*(?:(?:\.|->|::)[A-Za-z_][A-Za-z0-9_]*)*\s*\(.*\)\s*$)"));

    int statementDepth = 0;
    for (int lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        const QString syntaxLine = syntaxLines.value(lineNumber);
        const QString code = syntaxLine.trimmed();
        if (code.isEmpty() || code.startsWith(QLatin1Char('#'))) continue;

        const int depthBeforeLine = statementDepth;
        for (const QChar character : syntaxLine) {
            if (character == QLatin1Char('(') || character == QLatin1Char('['))
                ++statementDepth;
            else if (character == QLatin1Char(')') || character == QLatin1Char(']'))
                statementDepth = qMax(0, statementDepth - 1);
        }

        const auto control = controlStatement.match(code);
        if (control.hasMatch()) {
            QString remainder = control.captured(2).trimmed();
            if (control.captured(1) == QStringLiteral("if")
                && remainder.startsWith(QStringLiteral("constexpr"))) {
                remainder = remainder.mid(9).trimmed();
            }
            if (!remainder.startsWith(QLatin1Char('('))) {
                result.push_back(diagnostic(lineNumber,
                    qMax(0, lines.at(lineNumber).indexOf(control.captured(1)) + control.captured(1).size()),
                    QStringLiteral("Expected '(' after %1").arg(control.captured(1))));
            }
        }
        if (depthBeforeLine > 0 || statementDepth > 0) continue;

        if (code.endsWith(QLatin1Char(';')) || code.endsWith(QLatin1Char('{'))
            || code.endsWith(QLatin1Char('}')) || code.endsWith(QLatin1Char(':'))
            || code.endsWith(QLatin1Char(',')) || code.endsWith(QLatin1Char('\\'))
            || control.hasMatch() || code == QStringLiteral("else")
            || code == QStringLiteral("do") || code == QStringLiteral("try")) continue;

        bool nextLineStartsBlock = false;
        for (int nextLine = lineNumber + 1; nextLine < syntaxLines.size(); ++nextLine) {
            const QString nextCode = syntaxLines.at(nextLine).trimmed();
            if (nextCode.isEmpty()) continue;
            nextLineStartsBlock = nextCode.startsWith(QLatin1Char('{'));
            break;
        }
        if (nextLineStartsBlock && code.endsWith(QLatin1Char(')'))) continue;

        if (definiteStatement.match(code).hasMatch() || declaration.match(code).hasMatch()
            || assignment.match(code).hasMatch() || callExpression.match(code).hasMatch()) {
            result.push_back(diagnostic(lineNumber, qMax(0, lines.at(lineNumber).size() - 1),
                                        QStringLiteral("Expected ';'")));
        }
    }

    struct ConditionalDirective { int line; bool sawElse; };
    QList<ConditionalDirective> conditionals;
    const QRegularExpression directive(QStringLiteral(
        R"(^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b)"));
    for (int lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        const auto match = directive.match(lines.at(lineNumber));
        if (!match.hasMatch()) continue;
        const QString kind = match.captured(1);
        if (kind == QStringLiteral("if") || kind == QStringLiteral("ifdef")
            || kind == QStringLiteral("ifndef")) {
            conditionals.push_back({lineNumber, false});
        } else if (kind == QStringLiteral("endif")) {
            if (conditionals.isEmpty())
                result.push_back(diagnostic(lineNumber, 0, QStringLiteral("#endif has no matching #if")));
            else
                conditionals.removeLast();
        } else if (conditionals.isEmpty()) {
            result.push_back(diagnostic(lineNumber, 0,
                QStringLiteral("#%1 has no matching #if").arg(kind)));
        } else if (kind == QStringLiteral("else")) {
            if (conditionals.last().sawElse)
                result.push_back(diagnostic(lineNumber, 0, QStringLiteral("Duplicate #else")));
            conditionals.last().sawElse = true;
        } else if (conditionals.last().sawElse) {
            result.push_back(diagnostic(lineNumber, 0, QStringLiteral("#elif after #else")));
        }
    }
    for (const auto &opening : conditionals)
        result.push_back(diagnostic(opening.line, 0,
                                    QStringLiteral("Preprocessor conditional is missing #endif")));
    return result;
}
