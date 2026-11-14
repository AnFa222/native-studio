#pragma once

#include "languageplugin.h"

#include <QPlainTextEdit>

class QCompleter;
class QEvent;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QStringListModel;
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
    void setCompletionItems(const QStringList &items);
    void setDiagnostics(const QList<CodeDiagnostic> &diagnostics);
    [[nodiscard]] const QList<CodeDiagnostic> &diagnostics() const;
    [[nodiscard]] QString inlineCompletion() const;

signals:
    void cursorPositionDescriptionChanged(const QString &description);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int verticalDelta);
    void highlightCurrentLine();
    void refreshExtraSelections();
    void updateInlineCompletion();
    void showCompletionPopup();
    void insertCompletion(const QString &completion);
    [[nodiscard]] QString currentWordPrefix() const;
    [[nodiscard]] const CodeDiagnostic *diagnosticAt(int documentPosition) const;

    QString m_filePath;
    QSyntaxHighlighter *m_highlighter = nullptr;
    QWidget *m_lineNumberArea = nullptr;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_completionModel = nullptr;
    QString m_inlineCompletion;
    QList<CodeDiagnostic> m_diagnostics;
};
