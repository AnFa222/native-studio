#pragma once

#include "languageplugin.h"

#include <QColor>
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
    [[nodiscard]] bool completionPopupVisible() const;
    void reloadSettings();

signals:
    void cursorPositionDescriptionChanged(const QString &description);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int verticalDelta);
    void highlightCurrentLine();
    void refreshExtraSelections();
    void updateCompletionCandidate();
    void showCompletionPopup();
    void insertCompletion(const QString &completion);
    void useCompletionCandidate(const QString &completion);
    bool selectBestCompletion(const QString &prefix);
    [[nodiscard]] QString currentWordPrefix() const;
    [[nodiscard]] const CodeDiagnostic *diagnosticAt(int documentPosition) const;

    QString m_filePath;
    QSyntaxHighlighter *m_highlighter = nullptr;
    QWidget *m_lineNumberArea = nullptr;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_completionModel = nullptr;
    QString m_completionCandidate;
    bool m_completionDismissed = false;
    bool m_autoClosePairs = true;
    bool m_showLineNumbers = true;
    QColor m_currentLineColor;
    QColor m_gutterBackground;
    QColor m_gutterBorder;
    QColor m_lineNumberColor;
    QColor m_activeLineNumberColor;
    QList<CodeDiagnostic> m_diagnostics;
};
