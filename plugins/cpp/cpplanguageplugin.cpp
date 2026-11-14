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
    auto matches = identifier.globalMatch(documentText);
    while (matches.hasNext()) items.insert(matches.next().captured());

    QStringList result(items.begin(), items.end());
    result.sort(Qt::CaseInsensitive);
    return result;
}

QList<CodeDiagnostic> CppLanguagePlugin::diagnostics(const QString &documentText) const
{
    QList<CodeDiagnostic> result;
    QList<Delimiter> delimiters;
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

    QList<Delimiter> conditionals;
    const auto lines = documentText.split(QLatin1Char('\n'));
    const QRegularExpression openingDirective(QStringLiteral(R"(^\s*#\s*(if|ifdef|ifndef)\b)"));
    const QRegularExpression closingDirective(QStringLiteral(R"(^\s*#\s*endif\b)"));
    for (int lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        if (openingDirective.match(lines.at(lineNumber)).hasMatch()) {
            conditionals.push_back({QLatin1Char('#'), lineNumber, 0});
        } else if (closingDirective.match(lines.at(lineNumber)).hasMatch()) {
            if (conditionals.isEmpty()) {
                result.push_back(diagnostic(lineNumber, 0, QStringLiteral("#endif has no matching #if")));
            } else {
                conditionals.removeLast();
            }
        }
    }
    for (const auto &opening : conditionals)
        result.push_back(diagnostic(opening.line, opening.column,
                                    QStringLiteral("Preprocessor conditional is missing #endif")));
    return result;
}
