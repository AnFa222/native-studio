#include "pythonhighlighter.h"

#include <QColor>
#include <QFont>
#include <QSettings>
#include <QStringList>
#include <QTextDocument>

PythonHighlighter::PythonHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    const bool light = QSettings().value(QStringLiteral("appearance/theme"),
                                         QStringLiteral("dark")).toString()
        == QStringLiteral("light");
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(light ? QStringLiteral("#8250df")
                                             : QStringLiteral("#c586c0")));
    keywordFormat.setFontWeight(QFont::DemiBold);
    const QStringList keywords{
        QStringLiteral("and"), QStringLiteral("as"), QStringLiteral("assert"),
        QStringLiteral("async"), QStringLiteral("await"), QStringLiteral("break"),
        QStringLiteral("case"), QStringLiteral("class"), QStringLiteral("continue"),
        QStringLiteral("def"), QStringLiteral("del"), QStringLiteral("elif"),
        QStringLiteral("else"), QStringLiteral("except"), QStringLiteral("finally"),
        QStringLiteral("for"), QStringLiteral("from"), QStringLiteral("global"),
        QStringLiteral("if"), QStringLiteral("import"), QStringLiteral("in"),
        QStringLiteral("is"), QStringLiteral("lambda"), QStringLiteral("match"),
        QStringLiteral("nonlocal"), QStringLiteral("not"), QStringLiteral("or"),
        QStringLiteral("pass"), QStringLiteral("raise"), QStringLiteral("return"),
        QStringLiteral("try"), QStringLiteral("while"), QStringLiteral("with"),
        QStringLiteral("yield")};
    for (const auto &keyword : keywords) {
        m_rules.push_back({QRegularExpression(QStringLiteral("\\b%1\\b").arg(keyword)), keywordFormat});
    }

    QTextCharFormat builtinFormat;
    builtinFormat.setForeground(QColor(light ? QStringLiteral("#1f6f78")
                                             : QStringLiteral("#4ec9b0")));
    const QStringList builtins{
        QStringLiteral("bool"), QStringLiteral("bytes"), QStringLiteral("dict"),
        QStringLiteral("enumerate"), QStringLiteral("filter"), QStringLiteral("float"),
        QStringLiteral("int"), QStringLiteral("len"), QStringLiteral("list"),
        QStringLiteral("map"), QStringLiteral("object"), QStringLiteral("open"),
        QStringLiteral("print"), QStringLiteral("range"), QStringLiteral("set"),
        QStringLiteral("str"), QStringLiteral("super"), QStringLiteral("tuple"),
        QStringLiteral("type"), QStringLiteral("zip")};
    for (const auto &builtin : builtins) {
        m_rules.push_back({QRegularExpression(QStringLiteral("\\b%1(?=\\s*\\()").arg(builtin)), builtinFormat});
    }

    QTextCharFormat constantFormat;
    constantFormat.setForeground(QColor(light ? QStringLiteral("#0969da")
                                              : QStringLiteral("#569cd6")));
    m_rules.push_back({QRegularExpression(QStringLiteral("\\b(?:True|False|None|NotImplemented|Ellipsis)\\b")),
                       constantFormat});

    QTextCharFormat decoratorFormat;
    decoratorFormat.setForeground(QColor(light ? QStringLiteral("#9a6700")
                                               : QStringLiteral("#dcdcaa")));
    m_rules.push_back({QRegularExpression(QStringLiteral("@[_A-Za-z][_A-Za-z0-9.]*")), decoratorFormat});

    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(light ? QStringLiteral("#116329")
                                            : QStringLiteral("#b5cea8")));
    m_rules.push_back({QRegularExpression(
        QStringLiteral("\\b(?:0[xX][0-9a-fA-F_]+|0[bB][01_]+|0[oO][0-7_]+|[0-9][0-9_]*(?:\\.[0-9_]*)?(?:[eE][+-]?[0-9_]+)?j?)\\b")),
        numberFormat});

    m_stringFormat.setForeground(QColor(light ? QStringLiteral("#a40e26")
                                              : QStringLiteral("#ce9178")));
    m_rules.push_back({QRegularExpression(QStringLiteral("(?:[rRuUbBfF]{0,2})\"(?:\\\\.|[^\"\\\\])*\"")),
                       m_stringFormat});
    m_rules.push_back({QRegularExpression(QStringLiteral("(?:[rRuUbBfF]{0,2})'(?:\\\\.|[^'\\\\])*'")),
                       m_stringFormat});

    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor(light ? QStringLiteral("#1a7f37")
                                             : QStringLiteral("#6a9955")));
    commentFormat.setFontItalic(true);
    m_rules.push_back({QRegularExpression(QStringLiteral("#.*$")), commentFormat});
}

void PythonHighlighter::highlightBlock(const QString &text)
{
    for (const auto &rule : m_rules) {
        auto matches = rule.pattern.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    setCurrentBlockState(0);
    highlightMultilineStrings(text, QStringLiteral("\"\"\""), 1, m_stringFormat);
    if (currentBlockState() == 0) {
        highlightMultilineStrings(text, QStringLiteral("'''"), 2, m_stringFormat);
    }
}

void PythonHighlighter::highlightMultilineStrings(const QString &text, const QString &delimiter,
                                                   int state, const QTextCharFormat &format)
{
    bool continuingFromPreviousBlock = previousBlockState() == state;
    int start = continuingFromPreviousBlock ? 0 : text.indexOf(delimiter);
    while (start >= 0) {
        const int searchFrom = start + (continuingFromPreviousBlock ? 0 : delimiter.size());
        const int end = text.indexOf(delimiter, searchFrom);
        const int length = end < 0 ? text.size() - start : end - start + delimiter.size();
        setFormat(start, length, format);
        if (end < 0) {
            setCurrentBlockState(state);
            return;
        }
        continuingFromPreviousBlock = false;
        start = text.indexOf(delimiter, start + length);
    }
}
