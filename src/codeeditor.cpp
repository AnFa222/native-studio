#include "codeeditor.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QSyntaxHighlighter>

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    setFont(font);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4.0);
    setPlaceholderText(tr("Start typing..."));

    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        const auto cursor = textCursor();
        emit cursorPositionDescriptionChanged(
            tr("Line %1, Column %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
    });
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
