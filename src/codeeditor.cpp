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
#include <QSettings>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextStream>
#include <QToolTip>
#include <QSyntaxHighlighter>

namespace {
QChar closingCharacterFor(const QChar opening)
{
    switch (opening.unicode()) {
    case '(': return QLatin1Char(')');
    case '[': return QLatin1Char(']');
    case '{': return QLatin1Char('}');
    case '\'': return QLatin1Char('\'');
    case '"': return QLatin1Char('"');
    case '`': return QLatin1Char('`');
    default: return {};
    }
}

bool isClosingCharacter(const QChar character)
{
    return character == QLatin1Char(')') || character == QLatin1Char(']')
        || character == QLatin1Char('}') || character == QLatin1Char('\'')
        || character == QLatin1Char('"') || character == QLatin1Char('`');
}

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
    reloadSettings();
    setPlaceholderText(tr("Start typing..."));

    m_completer->setModel(m_completionModel);
    m_completer->setWidget(this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setFilterMode(Qt::MatchStartsWith);
    m_completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    m_completer->setMaxVisibleItems(10);
    m_completer->popup()->setObjectName(QStringLiteral("completionPopup"));
    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated),
            this, &CodeEditor::insertCompletion);
    connect(m_completer, QOverload<const QString &>::of(&QCompleter::highlighted),
            this, &CodeEditor::useCompletionCandidate);

    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        highlightCurrentLine();
        updateCompletionCandidate();
        const auto cursor = textCursor();
        emit cursorPositionDescriptionChanged(
            tr("Line %1, Column %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
    });
    connect(this, &QPlainTextEdit::textChanged, this, &CodeEditor::updateCompletionCandidate);

    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() const
{
    if (!m_showLineNumbers) return 0;
    int digits = 1;
    for (int lines = qMax(1, blockCount()); lines >= 10; lines /= 10) ++digits;
    return 18 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::reloadSettings()
{
    QSettings settings;
    QFont editorFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    editorFont.setFamily(settings.value(QStringLiteral("editor/fontFamily"),
                                         editorFont.family()).toString());
    editorFont.setPointSize(qBound(8, settings.value(QStringLiteral("editor/fontSize"), 11).toInt(),
                                   32));
    setFont(editorFont);
    m_tabSize = qBound(1, settings.value(QStringLiteral("editor/tabSize"), 4).toInt(), 16);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * m_tabSize);
    setLineWrapMode(settings.value(QStringLiteral("editor/wordWrap"), false).toBool()
        ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    m_autoClosePairs = settings.value(QStringLiteral("editor/autoClosePairs"), true).toBool();
    m_autoIndent = settings.value(QStringLiteral("editor/autoIndent"), true).toBool();
    m_showLineNumbers = settings.value(QStringLiteral("editor/lineNumbers"), true).toBool();
    m_lineNumberArea->setVisible(m_showLineNumbers);
    const bool light = settings.value(QStringLiteral("appearance/theme"),
                                      QStringLiteral("dark")).toString() == QStringLiteral("light");
    m_currentLineColor = QColor(light ? QStringLiteral("#eef4ff") : QStringLiteral("#172033"));
    m_gutterBackground = QColor(light ? QStringLiteral("#f3f4f6") : QStringLiteral("#090d13"));
    m_gutterBorder = QColor(light ? QStringLiteral("#d0d7de") : QStringLiteral("#21262d"));
    m_lineNumberColor = QColor(light ? QStringLiteral("#57606a") : QStringLiteral("#6e7681"));
    m_activeLineNumberColor = QColor(light ? QStringLiteral("#0969da") : QStringLiteral("#f0f6fc"));
    QPalette editorPalette = palette();
    editorPalette.setColor(QPalette::Base, QColor(light ? QStringLiteral("#ffffff")
                                                   : QStringLiteral("#0d1117")));
    editorPalette.setColor(QPalette::Text, QColor(light ? QStringLiteral("#24292f")
                                                   : QStringLiteral("#e6edf3")));
    setPalette(editorPalette);
    updateLineNumberAreaWidth();
    refreshExtraSelections();
    viewport()->update();
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
    selection.format.setBackground(m_currentLineColor);
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
    painter.fillRect(event->rect(), m_gutterBackground);
    painter.setPen(m_gutterBorder);
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
                           : blockNumber == currentBlock ? m_activeLineNumberColor
                                                         : m_lineNumberColor);
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
    if (normalized == m_completionModel->stringList()) {
        updateCompletionCandidate();
        return;
    }

    // Background plugin analysis also refreshes document symbols. Replacing a
    // QCompleter model while its popup is open closes the active session, so
    // keep the stable vocabulary until the user accepts, dismisses, or leaves
    // the current identifier. The next analysis pass will apply fresh symbols.
    if (m_completer->popup()->isVisible() && !currentWordPrefix().isEmpty()) return;

    m_completionModel->setStringList(normalized);
    updateCompletionCandidate();
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

bool CodeEditor::completionPopupVisible() const
{
    return m_completer->popup()->isVisible();
}

void CodeEditor::setIndentationDeltaProvider(std::function<int(const QString &)> provider)
{
    m_indentationDeltaProvider = std::move(provider);
}

bool CodeEditor::insertAutoIndentedNewLine()
{
    if (!m_autoIndent) return false;
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    if (cursor.hasSelection()) cursor.removeSelectedText();

    const QString lineBeforeCursor = cursor.block().text().left(cursor.positionInBlock());
    QString indentation;
    while (indentation.size() < lineBeforeCursor.size()) {
        const QChar character = lineBeforeCursor.at(indentation.size());
        if (character != QLatin1Char(' ') && character != QLatin1Char('\t')) break;
        indentation += character;
    }

    const int delta = m_indentationDeltaProvider
        ? qMax(0, m_indentationDeltaProvider(lineBeforeCursor)) : 0;
    const QString nestedIndent = indentation + QString(delta * m_tabSize, QLatin1Char(' '));
    const QString contents = toPlainText();
    const bool splitClosingBrace = delta > 0 && cursor.position() < contents.size()
        && contents.at(cursor.position()) == QLatin1Char('}');
    if (splitClosingBrace) {
        cursor.insertText(QLatin1Char('\n') + nestedIndent + QLatin1Char('\n') + indentation);
        cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, indentation.size() + 1);
    } else {
        cursor.insertText(QLatin1Char('\n') + nestedIndent);
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
    m_completer->popup()->hide();
    m_completionDismissed = false;
    updateCompletionCandidate();
    return true;
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

void CodeEditor::updateCompletionCandidate()
{
    m_completionCandidate.clear();
    const QString prefix = currentWordPrefix();
    if (prefix.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }

    m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() > 0 && selectBestCompletion(prefix)) {
        useCompletionCandidate(m_completer->currentCompletion());
    }
}

void CodeEditor::showCompletionPopup()
{
    const QString prefix = currentWordPrefix();
    if (prefix.isEmpty()) {
        m_completer->setCompletionPrefix({});
    } else if (m_completer->completionPrefix() != prefix) {
        m_completer->setCompletionPrefix(prefix);
    }
    if (m_completer->completionCount() == 0) {
        m_completer->popup()->hide();
        return;
    }
    if (!selectBestCompletion(prefix)) {
        m_completer->popup()->hide();
        return;
    }
    m_completer->popup()->setCurrentIndex(m_completer->currentIndex());
    useCompletionCandidate(m_completer->currentCompletion());
    QRect area = cursorRect();
    area.setWidth(qMax(320, m_completer->popup()->sizeHintForColumn(0) + 36));
    m_completer->complete(area);
}

bool CodeEditor::selectBestCompletion(const QString &prefix)
{
    const int count = m_completer->completionCount();
    for (int row = 0; row < count; ++row) {
        if (!m_completer->setCurrentRow(row)) continue;
        const QString candidate = m_completer->currentCompletion();
        if (candidate.startsWith(prefix, Qt::CaseInsensitive)
            && candidate.size() > prefix.size()) return true;
    }
    return count > 0 && m_completer->setCurrentRow(0);
}

void CodeEditor::useCompletionCandidate(const QString &completion)
{
    m_completionCandidate = completion;
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
    m_completionDismissed = false;
    updateCompletionCandidate();
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    const bool acceptKey = event->key() == Qt::Key_Tab
        || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (acceptKey && !m_completionDismissed && m_completer->popup()->isVisible()) {
        const QString completion = m_completionCandidate;
        if (!completion.isEmpty()) insertCompletion(completion);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space && event->modifiers().testFlag(Qt::ControlModifier)) {
        m_completionDismissed = false;
        updateCompletionCandidate();
        showCompletionPopup();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_completer->popup()->isVisible()) {
        m_completer->popup()->hide();
        m_completionDismissed = true;
        m_completionCandidate.clear();
        event->accept();
        return;
    }

    const bool hasCommandModifier = event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::AltModifier)
        || event->modifiers().testFlag(Qt::MetaModifier);
    if (!hasCommandModifier
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && insertAutoIndentedNewLine()) {
        event->accept();
        return;
    }
    if (m_autoClosePairs && !hasCommandModifier && event->text().size() == 1) {
        const QChar typed = event->text().front();
        QTextCursor cursor = textCursor();
        const QString contents = toPlainText();
        const bool closingAhead = !cursor.hasSelection()
            && cursor.position() < contents.size()
            && contents.at(cursor.position()) == typed;
        if (isClosingCharacter(typed) && closingAhead) {
            cursor.movePosition(QTextCursor::Right);
            setTextCursor(cursor);
            m_completer->popup()->hide();
            updateCompletionCandidate();
            event->accept();
            return;
        }

        const QChar closing = closingCharacterFor(typed);
        if (!closing.isNull()) {
            const int selectionStart = cursor.selectionStart();
            const QString selected = cursor.selectedText();
            cursor.beginEditBlock();
            cursor.insertText(QString(typed) + selected + QString(closing));
            cursor.endEditBlock();
            if (selected.isEmpty()) {
                cursor.movePosition(QTextCursor::Left);
            } else {
                cursor.setPosition(selectionStart + 1);
                cursor.setPosition(selectionStart + 1 + selected.size(), QTextCursor::KeepAnchor);
            }
            setTextCursor(cursor);
            m_completer->popup()->hide();
            m_completionDismissed = false;
            updateCompletionCandidate();
            event->accept();
            return;
        }
    }

    QPlainTextEdit::keyPressEvent(event);
    const bool deletionKey = event->key() == Qt::Key_Backspace
        || event->key() == Qt::Key_Delete;
    if (!event->text().isEmpty() || deletionKey) {
        m_completionDismissed = false;
    }
    updateCompletionCandidate();
    if (!m_completionDismissed && !currentWordPrefix().isEmpty()) {
        const bool typedIdentifier = !event->text().isEmpty()
            && (event->text().front().isLetterOrNumber()
                || event->text().front() == QLatin1Char('_'));
        if (typedIdentifier || deletionKey) showCompletionPopup();
    }
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
