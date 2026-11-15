#include "cpphighlighter.h"

#include <QColor>
#include <QFont>
#include <QSettings>
#include <QStringList>
#include <QTextDocument>

CppHighlighter::CppHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    const bool light = QSettings().value(QStringLiteral("appearance/theme"),
                                         QStringLiteral("dark")).toString()
        == QStringLiteral("light");
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(light ? QStringLiteral("#0969da")
                                             : QStringLiteral("#569cd6")));
    keywordFormat.setFontWeight(QFont::DemiBold);
    const QStringList keywords{
        QStringLiteral("alignas"), QStringLiteral("alignof"), QStringLiteral("auto"),
        QStringLiteral("bool"), QStringLiteral("break"), QStringLiteral("case"),
        QStringLiteral("catch"), QStringLiteral("char"), QStringLiteral("class"),
        QStringLiteral("concept"), QStringLiteral("const"), QStringLiteral("consteval"),
        QStringLiteral("constexpr"), QStringLiteral("constinit"), QStringLiteral("continue"),
        QStringLiteral("co_await"), QStringLiteral("co_return"), QStringLiteral("co_yield"),
        QStringLiteral("default"), QStringLiteral("delete"), QStringLiteral("do"),
        QStringLiteral("double"), QStringLiteral("else"), QStringLiteral("enum"),
        QStringLiteral("explicit"), QStringLiteral("export"), QStringLiteral("extern"),
        QStringLiteral("false"), QStringLiteral("float"), QStringLiteral("for"),
        QStringLiteral("friend"), QStringLiteral("if"), QStringLiteral("inline"),
        QStringLiteral("int"), QStringLiteral("long"), QStringLiteral("module"),
        QStringLiteral("mutable"), QStringLiteral("namespace"), QStringLiteral("new"),
        QStringLiteral("noexcept"), QStringLiteral("nullptr"), QStringLiteral("operator"),
        QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("public"),
        QStringLiteral("requires"), QStringLiteral("return"), QStringLiteral("short"),
        QStringLiteral("signed"), QStringLiteral("sizeof"), QStringLiteral("static"),
        QStringLiteral("struct"), QStringLiteral("switch"), QStringLiteral("template"),
        QStringLiteral("this"), QStringLiteral("throw"), QStringLiteral("true"),
        QStringLiteral("try"), QStringLiteral("typedef"), QStringLiteral("typename"),
        QStringLiteral("union"), QStringLiteral("unsigned"), QStringLiteral("using"),
        QStringLiteral("virtual"), QStringLiteral("void"), QStringLiteral("volatile"),
        QStringLiteral("while")};
    for (const auto &keyword : keywords) {
        m_rules.push_back({QRegularExpression(QStringLiteral("\\b%1\\b").arg(keyword)), keywordFormat});
    }

    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(light ? QStringLiteral("#a40e26")
                                            : QStringLiteral("#ce9178")));
    m_rules.push_back({QRegularExpression(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"")), stringFormat});
    m_rules.push_back({QRegularExpression(QStringLiteral("'(?:\\\\.|[^'\\\\])*'")), stringFormat});

    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(light ? QStringLiteral("#116329")
                                            : QStringLiteral("#b5cea8")));
    m_rules.push_back({QRegularExpression(QStringLiteral("\\b(?:0[xX][0-9a-fA-F']+|[0-9][0-9']*(?:\\.[0-9']+)?)\\b")), numberFormat});

    QTextCharFormat preprocessorFormat;
    preprocessorFormat.setForeground(QColor(light ? QStringLiteral("#8250df")
                                                  : QStringLiteral("#c586c0")));
    m_rules.push_back({QRegularExpression(QStringLiteral("^\\s*#.*$")), preprocessorFormat});

    m_commentFormat.setForeground(QColor(light ? QStringLiteral("#1a7f37")
                                                : QStringLiteral("#6a9955")));
    m_commentFormat.setFontItalic(true);
    m_rules.push_back({QRegularExpression(QStringLiteral("//.*$")), m_commentFormat});
}

void CppHighlighter::highlightBlock(const QString &text)
{
    for (const auto &rule : m_rules) {
        auto iterator = rule.pattern.globalMatch(text);
        while (iterator.hasNext()) {
            const auto match = iterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    setCurrentBlockState(0);
    int start = previousBlockState() == 1 ? 0 : text.indexOf(QStringLiteral("/*"));
    while (start >= 0) {
        const int end = text.indexOf(QStringLiteral("*/"), start + 2);
        const int length = end < 0 ? text.size() - start : end - start + 2;
        setFormat(start, length, m_commentFormat);
        if (end < 0) {
            setCurrentBlockState(1);
            break;
        }
        start = text.indexOf(QStringLiteral("/*"), start + length);
    }
}
