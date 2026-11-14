#pragma once

#include <QPlainTextEdit>

class QSyntaxHighlighter;

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

signals:
    void cursorPositionDescriptionChanged(const QString &description);

private:
    QString m_filePath;
    QSyntaxHighlighter *m_highlighter = nullptr;
};
