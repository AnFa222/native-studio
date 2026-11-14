#include "codeeditor.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QResizeEvent>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextStream>
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
{
    setObjectName(QStringLiteral("codeEditor"));
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    setFont(font);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4.0);
    setPlaceholderText(tr("Start typing..."));

    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        highlightCurrentLine();
        const auto cursor = textCursor();
        emit cursorPositionDescriptionChanged(
            tr("Line %1, Column %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
    });

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
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor(QStringLiteral("#172033")));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    setExtraSelections({selection});
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
            painter.setPen(blockNumber == currentBlock
                               ? QColor(QStringLiteral("#f0f6fc"))
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
