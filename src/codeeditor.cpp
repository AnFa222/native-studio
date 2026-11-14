#include "codeeditor.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFile>
#include <QFileInfo>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSaveFile>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextStream>
#include <QToolTip>
#include <QSyntaxHighlighter>

namespace {
class LineNumberArea final : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor)
        : QWidget(editor), m_editor(editor)
    {
        setObjectName(QStringLiteral("lineNumberArea"));
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return {m_editor->lineNumberAreaWidth(), 0};
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        m_editor->paintLineNumberArea(event);
    }

private:
    CodeEditor *m_editor;
};
}

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_lineNumberArea(new LineNumberArea(this))
    , m_completer(new QCompleter(this))
    , m_completionModel(new QStringListModel(this))
{
    setObjectName(QStringLiteral("codeEditor"));
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    setFont(font);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4.0);
    setPlaceholderText(tr("Start typing..."));

    m_completer->setModel(m_completionModel);
    m_completer->setWidget(this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setFilterMode(Qt::MatchStartsWith);
    m_completer->popup()->setObjectName(QStringLiteral("completionPopup"));
    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated),
            this, &CodeEditor::insertCompletion);

    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        highlightCurrentLine();
        updateInlineCompletion();
        const auto cursor = textCursor();
        emit cursorPositionDescriptionChanged(
            tr("Line %1, Column %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
    });
    connect(this, &QPlainTextEdit::textChanged, this, &CodeEditor::updateInlineCompletion);

    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    for (int lines = qMax(1, blockCount()); lines >= 10; lines /= 10) ++digits;
    return 18 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int verticalDelta)
{
    if (verticalDelta != 0) {
        m_lineNumberArea->scroll(0, verticalDelta);
    } else {
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    }

    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect contents = contentsRect();
    m_lineNumberArea->setGeometry(contents.left(), contents.top(),
                                  lineNumberAreaWidth(), contents.height());
}

void CodeEditor::highlightCurrentLine()
{
    refreshExtraSelections();
}

void CodeEditor::refreshExtraSelections()
{
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor(QStringLiteral("#172033")));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    selections.push_back(selection);

    for (const auto &problem : m_diagnostics) {
        const QTextBlock block = document()->findBlockByNumber(problem.line);
        if (!block.isValid()) continue;
        const int start = block.position() + qBound(0, problem.column, block.length() - 1);
        const int end = qMin(start + qMax(1, problem.length), block.position() + block.length() - 1);
        QTextEdit::ExtraSelection diagnosticSelection;
        diagnosticSelection.cursor = QTextCursor(document());
        diagnosticSelection.cursor.setPosition(start);
        diagnosticSelection.cursor.setPosition(qMax(start + 1, end), QTextCursor::KeepAnchor);
        diagnosticSelection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        diagnosticSelection.format.setUnderlineColor(
            problem.severity == DiagnosticSeverity::Error
                ? QColor(QStringLiteral("#f85149")) : QColor(QStringLiteral("#d29922")));
        diagnosticSelection.format.setToolTip(problem.message);
        selections.push_back(diagnosticSelection);
    }
    setExtraSelections(selections);
}

void CodeEditor::paintLineNumberArea(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), QColor(QStringLiteral("#090d13")));
    painter.setPen(QColor(QStringLiteral("#21262d")));
    painter.drawLine(m_lineNumberArea->width() - 1, event->rect().top(),
                     m_lineNumberArea->width() - 1, event->rect().bottom());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int currentBlock = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            bool hasError = false;
            bool hasWarning = false;
            for (const auto &problem : m_diagnostics) {
                if (problem.line != blockNumber) continue;
                hasError |= problem.severity == DiagnosticSeverity::Error;
                hasWarning |= problem.severity == DiagnosticSeverity::Warning;
            }
            painter.setPen(hasError ? QColor(QStringLiteral("#f85149"))
                           : hasWarning ? QColor(QStringLiteral("#d29922"))
                           : blockNumber == currentBlock ? QColor(QStringLiteral("#f0f6fc"))
                                                         : QColor(QStringLiteral("#6e7681")));
            painter.drawText(0, top, m_lineNumberArea->width() - 8,
                             fontMetrics().height(), Qt::AlignRight,
                             QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::setCompletionItems(const QStringList &items)
{
    QStringList normalized = items;
    normalized.removeDuplicates();
    normalized.sort(Qt::CaseInsensitive);
    m_completionModel->setStringList(normalized);
    updateInlineCompletion();
}

void CodeEditor::setDiagnostics(const QList<CodeDiagnostic> &diagnostics)
{
    m_diagnostics = diagnostics;
    refreshExtraSelections();
    m_lineNumberArea->update();
    viewport()->update();
}

const QList<CodeDiagnostic> &CodeEditor::diagnostics() const
{
    return m_diagnostics;
}

QString CodeEditor::inlineCompletion() const
{
    return m_inlineCompletion;
}

QString CodeEditor::currentWordPrefix() const
{
    const QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) return {};
    const QString line = cursor.block().text();
    const int end = cursor.positionInBlock();
    int start = end;
    while (start > 0) {
        const QChar character = line.at(start - 1);
        if (!character.isLetterOrNumber() && character != QLatin1Char('_')) break;
        --start;
    }
    return line.mid(start, end - start);
}

void CodeEditor::updateInlineCompletion()
{
    m_inlineCompletion.clear();
    const QString prefix = currentWordPrefix();
    if (prefix.size() < 2) {
        viewport()->update();
        return;
    }
    for (const auto &candidate : m_completionModel->stringList()) {
        if (candidate.size() <= prefix.size()
            || !candidate.startsWith(prefix, Qt::CaseInsensitive)) continue;
        m_inlineCompletion = candidate.mid(prefix.size());
        if (candidate.startsWith(prefix, Qt::CaseSensitive)) break;
    }
    viewport()->update();
}

void CodeEditor::showCompletionPopup()
{
    const QString prefix = currentWordPrefix();
    m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() == 0) {
        m_completer->popup()->hide();
        return;
    }
    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    QRect area = cursorRect();
    area.setWidth(qMax(280, m_completer->popup()->sizeHintForColumn(0) + 24));
    m_completer->complete(area);
}

void CodeEditor::insertCompletion(const QString &completion)
{
    if (m_completer->widget() != this) return;
    const QString prefix = currentWordPrefix();
    QTextCursor cursor = textCursor();
    if (completion.startsWith(prefix, Qt::CaseInsensitive)) {
        cursor.insertText(completion.mid(prefix.size()));
    } else {
        cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, prefix.size());
        cursor.insertText(completion);
    }
    setTextCursor(cursor);
    m_completer->popup()->hide();
    updateInlineCompletion();
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab && !m_inlineCompletion.isEmpty()) {
        textCursor().insertText(m_inlineCompletion);
        m_completer->popup()->hide();
        updateInlineCompletion();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space && event->modifiers().testFlag(Qt::ControlModifier)) {
        showCompletionPopup();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_completer->popup()->isVisible()) {
        m_completer->popup()->hide();
        event->accept();
        return;
    }

    QPlainTextEdit::keyPressEvent(event);
    updateInlineCompletion();
    if (!event->text().isEmpty()) {
        const QChar typed = event->text().front();
        if ((typed.isLetterOrNumber() || typed == QLatin1Char('_'))
            && currentWordPrefix().size() >= 2) showCompletionPopup();
    }
}

void CodeEditor::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);
    if (m_inlineCompletion.isEmpty() || !hasFocus()) return;
    QPainter painter(viewport());
    painter.setPen(QColor(QStringLiteral("#6e7681")));
    const QRect caret = cursorRect();
    painter.drawText(caret.left(), caret.top() + fontMetrics().ascent(), m_inlineCompletion);
}

const CodeDiagnostic *CodeEditor::diagnosticAt(int documentPosition) const
{
    for (const auto &problem : m_diagnostics) {
        const QTextBlock block = document()->findBlockByNumber(problem.line);
        if (!block.isValid()) continue;
        const int start = block.position() + problem.column;
        if (documentPosition >= start && documentPosition <= start + qMax(1, problem.length))
            return &problem;
    }
    return nullptr;
}

bool CodeEditor::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto *help = static_cast<QHelpEvent *>(event);
        const auto *problem = diagnosticAt(cursorForPosition(help->pos()).position());
        if (problem) {
            QToolTip::showText(help->globalPos(), problem->message, this);
            return true;
        }
        QToolTip::hideText();
    }
    return QPlainTextEdit::event(event);
}

QString CodeEditor::filePath() const
{
    return m_filePath;
}

void CodeEditor::setFilePath(const QString &path)
{
    m_filePath = QFileInfo(path).absoluteFilePath();
}

bool CodeEditor::loadFromFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    setPlainText(stream.readAll());
    setFilePath(path);
    document()->setModified(false);
    return true;
}

bool CodeEditor::saveToFile(const QString &path, QString *error)
{
    const QString destination = path.isEmpty() ? m_filePath : QFileInfo(path).absoluteFilePath();
    if (destination.isEmpty()) {
        if (error) {
            *error = tr("No destination file was selected.");
        }
        return false;
    }

    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << toPlainText();
    stream.flush();
    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    setFilePath(destination);
    document()->setModified(false);
    return true;
}

void CodeEditor::setSyntaxHighlighter(QSyntaxHighlighter *highlighter)
{
    if (m_highlighter == highlighter) return;
    delete m_highlighter;
    m_highlighter = highlighter;
}
