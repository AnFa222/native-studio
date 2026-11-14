#pragma once

#include <QPlainTextEdit>

class QPaintEvent;
class QResizeEvent;
class QSyntaxHighlighter;
class QWidget;

class CodeEditor final : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    [[nodiscard]] QString filePath() const;
    void setFilePath(const QString &path);
    [[nodiscard]] bool loadFromFile(const QString &path, QString *error = nullptr);
    [[nodiscard]] bool saveToFile(const QString &path = {}, QString *error = nullptr);
    void setSyntaxHighlighter(QSyntaxHighlighter *highlighter);
    [[nodiscard]] int lineNumberAreaWidth() const;
    void paintLineNumberArea(QPaintEvent *event);

signals:
    void cursorPositionDescriptionChanged(const QString &description);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int verticalDelta);
    void highlightCurrentLine();

    QString m_filePath;
    QSyntaxHighlighter *m_highlighter = nullptr;
    QWidget *m_lineNumberArea = nullptr;
};
