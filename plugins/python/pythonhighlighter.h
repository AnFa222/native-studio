#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class PythonHighlighter final : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit PythonHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void highlightMultilineStrings(const QString &text, const QString &delimiter,
                                   int state, const QTextCharFormat &format);

    QVector<Rule> m_rules;
    QTextCharFormat m_stringFormat;
};
