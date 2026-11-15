#include "terminalwidget.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

TerminalWidget::TerminalWidget(QWidget *parent)
    : QWidget(parent)
    , m_output(new QPlainTextEdit(this))
    , m_terminateButton(new QToolButton(this))
    , m_shell(new QProcess(this))
    , m_runner(new QProcess(this))
    , m_workingDirectory(QDir::currentPath())
{
    const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_output->setObjectName(QStringLiteral("terminalOutput"));
    m_output->setReadOnly(false);
    m_output->setUndoRedoEnabled(false);
    m_output->setMaximumBlockCount(10000);
    m_output->setFont(font);
    m_output->setPlaceholderText(tr("Type commands directly in the terminal"));
    m_output->installEventFilter(this);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(8, 4, 6, 4);
    m_terminateButton->setObjectName(QStringLiteral("terminateTerminalButton"));
    m_terminateButton->setText(tr("■ Terminate"));
    m_terminateButton->setToolTip(tr("Terminate the running program or shell"));
    m_terminateButton->setAutoRaise(true);
    header->addStretch();
    header->addWidget(m_terminateButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_output);

    m_shell->setProcessChannelMode(QProcess::MergedChannels);
    m_runner->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_terminateButton, &QToolButton::clicked,
            this, &TerminalWidget::terminateActiveProcess);
    connect(m_shell, &QProcess::readyReadStandardOutput, this, &TerminalWidget::readShellOutput);
    connect(m_runner, &QProcess::readyReadStandardOutput, this, &TerminalWidget::readRunnerOutput);
    connect(m_shell, &QProcess::stateChanged, this, &TerminalWidget::updateTerminateButton);
    connect(m_runner, &QProcess::stateChanged, this, &TerminalWidget::updateTerminateButton);
    connect(m_shell, &QProcess::finished, this, [this] {
        m_shellStarted = false;
        if (m_shellTerminationRequested)
            appendMessage(tr("[Terminal shell stopped]\n"));
        updateTerminateButton();
    });
    connect(m_runner, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        readRunnerOutput();
        if (m_runnerTerminationRequested)
            appendMessage(tr("[Process terminated]\n"));
        else
            appendMessage(tr("\n[Process exited with code %1]\n").arg(exitCode));
        emit commandFinished(exitCode);
    });
    connect(m_shell, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_shellTerminationRequested && error == QProcess::Crashed) return;
        appendMessage(tr("[Terminal error: %1]\n").arg(m_shell->errorString()));
    });
    connect(m_runner, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_runnerTerminationRequested && error == QProcess::Crashed) return;
        appendMessage(tr("[Run error: %1]\n").arg(m_runner->errorString()));
    });
    reloadSettings();
    updateTerminateButton();
}

TerminalWidget::~TerminalWidget()
{
    m_runnerTerminationRequested = true;
    m_shellTerminationRequested = true;
    for (auto *process : {m_runner, m_shell}) {
        if (process->state() == QProcess::NotRunning) continue;
        process->terminate();
        if (!process->waitForFinished(500)) {
            process->kill();
            process->waitForFinished(500);
        }
    }
}

void TerminalWidget::startShell()
{
    if (m_shellStarted) return;
    m_shellStarted = true;
    m_shellTerminationRequested = false;
    m_shell->setWorkingDirectory(m_workingDirectory);
#ifdef Q_OS_WIN
    m_shell->start(QStringLiteral("cmd.exe"), {QStringLiteral("/Q")});
#else
    m_shell->start(QStringLiteral("/bin/sh"));
#endif
}

void TerminalWidget::startDeferred()
{
    startShell();
}

void TerminalWidget::reloadSettings()
{
    QSettings settings;
    QFont terminalFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    terminalFont.setFamily(settings.value(QStringLiteral("terminal/fontFamily"),
                                           terminalFont.family()).toString());
    terminalFont.setPointSize(qBound(8, settings.value(QStringLiteral("terminal/fontSize"),
                                                        qMax(8, terminalFont.pointSize())).toInt(), 32));
    m_output->setFont(terminalFont);
    const bool light = settings.value(QStringLiteral("appearance/theme"),
                                      QStringLiteral("dark")).toString() == QStringLiteral("light");
    const bool followTheme = settings.value(QStringLiteral("terminal/followTheme"), true).toBool();
    const QColor foreground(followTheme
        ? (light ? QStringLiteral("#24292f") : QStringLiteral("#ffffff"))
        : settings.value(QStringLiteral("terminal/foreground"),
                         QStringLiteral("#ffffff")).toString());
    const QColor background(followTheme
        ? (light ? QStringLiteral("#ffffff") : QStringLiteral("#010409"))
        : settings.value(QStringLiteral("terminal/background"),
                         QStringLiteral("#010409")).toString());
    m_output->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#terminalOutput { background: %1; color: %2; border: 0; "
        "selection-background-color: #1f6feb; selection-color: #ffffff; padding: 5px 8px; }")
        .arg(background.isValid() ? background.name() : QStringLiteral("#010409"),
             foreground.isValid() ? foreground.name() : QStringLiteral("#ffffff")));
}

void TerminalWidget::setWorkingDirectory(const QString &directory)
{
    if (!QFileInfo(directory).isDir()) return;
    m_workingDirectory = QFileInfo(directory).absoluteFilePath();
    if (m_shell->state() == QProcess::Running) {
#ifdef Q_OS_WIN
        const QString command = QStringLiteral("cd /d \"%1\"\r\n").arg(m_workingDirectory);
#else
        const QString command = QStringLiteral("cd \"%1\"\n").arg(m_workingDirectory);
#endif
        m_shell->write(command.toUtf8());
    }
}

QString TerminalWidget::currentInput() const
{
    QTextCursor cursor(m_output->document());
    cursor.setPosition(qBound(0, m_inputStart, m_output->document()->characterCount() - 1));
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    QString input = cursor.selectedText();
    input.replace(QChar(0x2029), QLatin1Char('\n'));
    return input;
}

void TerminalWidget::replaceCurrentInput(const QString &text)
{
    QTextCursor cursor(m_output->document());
    cursor.setPosition(qBound(0, m_inputStart, m_output->document()->characterCount() - 1));
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.insertText(text);
    m_output->setTextCursor(cursor);
}

void TerminalWidget::moveCursorToInput()
{
    QTextCursor cursor = m_output->textCursor();
    if (cursor.selectionStart() < m_inputStart || cursor.position() < m_inputStart) {
        cursor.clearSelection();
        cursor.movePosition(QTextCursor::End);
        m_output->setTextCursor(cursor);
    }
}

void TerminalWidget::submitCurrentInput()
{
    const QString input = currentInput();
    QTextCursor cursor(m_output->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("\n"));
    m_output->setTextCursor(cursor);
    m_inputStart = cursor.position();

    if (!input.trimmed().isEmpty()) {
        if (m_history.isEmpty() || m_history.constLast() != input) m_history.push_back(input);
        m_historyIndex = m_history.size();
    }
    const QByteArray payload = (input +
#ifdef Q_OS_WIN
                                QStringLiteral("\r\n")
#else
                                QStringLiteral("\n")
#endif
                                ).toUtf8();

    QProcess *target = m_runner->state() != QProcess::NotRunning ? m_runner : m_shell;
    if (target == m_shell) startShell();
    if (target->state() == QProcess::Starting) {
        connect(target, &QProcess::started, target, [target, payload] { target->write(payload); },
                Qt::SingleShotConnection);
    } else if (target->state() == QProcess::Running) {
        target->write(payload);
    }
}

void TerminalWidget::terminateActiveProcess()
{
    QProcess *target = nullptr;
    if (m_runner->state() != QProcess::NotRunning) {
        target = m_runner;
        m_runnerTerminationRequested = true;
    } else if (m_shell->state() != QProcess::NotRunning) {
        target = m_shell;
        m_shellTerminationRequested = true;
    }
    if (!target) return;

    appendMessage(target == m_runner ? tr("\n[Terminating running program…]\n")
                                     : tr("\n[Terminating terminal shell…]\n"));
    target->terminate();
    QTimer::singleShot(750, target, [target] {
        if (target->state() != QProcess::NotRunning) target->kill();
    });
}

void TerminalWidget::updateTerminateButton()
{
    m_terminateButton->setEnabled(m_runner->state() != QProcess::NotRunning
                                  || m_shell->state() != QProcess::NotRunning);
}

void TerminalWidget::executeProgram(const QString &program, const QStringList &arguments,
                                    const QString &workingDirectory)
{
    if (m_runner->state() != QProcess::NotRunning) {
        appendMessage(tr("[A program is already running.]\n"));
        return;
    }
    const QString directory = workingDirectory.isEmpty() ? m_workingDirectory : workingDirectory;
    m_runnerTerminationRequested = false;
    m_runner->setWorkingDirectory(directory);
    appendMessage(tr("\n[Running %1 %2]\n").arg(program, arguments.join(QLatin1Char(' '))));
    m_runner->start(program, arguments);
    moveCursorToInput();
    m_output->setFocus();
}

QString TerminalWidget::outputText() const { return m_output->toPlainText(); }
void TerminalWidget::readShellOutput() { appendOutput(m_shell->readAllStandardOutput()); }
void TerminalWidget::readRunnerOutput() { appendOutput(m_runner->readAllStandardOutput()); }

void TerminalWidget::appendOutput(const QByteArray &bytes)
{
    appendMessage(QString::fromLocal8Bit(bytes));
}

void TerminalWidget::appendMessage(const QString &message)
{
    const QString pendingInput = currentInput();
    QTextCursor cursor(m_output->document());
    cursor.setPosition(qBound(0, m_inputStart, m_output->document()->characterCount() - 1));
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.insertText(message);
    m_inputStart = cursor.position();
    cursor.insertText(pendingInput);
    m_output->setTextCursor(cursor);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

bool TerminalWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_output || event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    auto *key = static_cast<QKeyEvent *>(event);
    if (key->matches(QKeySequence::Copy)) {
        if (m_output->textCursor().hasSelection())
            return QWidget::eventFilter(watched, event);
        terminateActiveProcess();
        return true;
    }
    if (key->matches(QKeySequence::SelectAll)) {
        QTextCursor cursor(m_output->document());
        cursor.setPosition(qBound(0, m_inputStart, m_output->document()->characterCount() - 1));
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        m_output->setTextCursor(cursor);
        return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        submitCurrentInput();
        return true;
    }
    if (key->key() == Qt::Key_Up && key->modifiers() == Qt::NoModifier) {
        if (!m_history.isEmpty()) {
            m_historyIndex = qMax(0, m_historyIndex - 1);
            replaceCurrentInput(m_history.at(m_historyIndex));
        }
        return true;
    }
    if (key->key() == Qt::Key_Down && key->modifiers() == Qt::NoModifier) {
        if (m_historyIndex < m_history.size() - 1) {
            replaceCurrentInput(m_history.at(++m_historyIndex));
        } else {
            m_historyIndex = m_history.size();
            replaceCurrentInput({});
        }
        return true;
    }
    if (key->key() == Qt::Key_Home && key->modifiers() == Qt::NoModifier) {
        QTextCursor cursor = m_output->textCursor();
        cursor.setPosition(m_inputStart);
        m_output->setTextCursor(cursor);
        return true;
    }
    if ((key->key() == Qt::Key_Backspace || key->key() == Qt::Key_Left)
        && !m_output->textCursor().hasSelection()
        && m_output->textCursor().position() <= m_inputStart) return true;

    const bool editsText = !key->text().isEmpty() || key->key() == Qt::Key_Backspace
        || key->key() == Qt::Key_Delete || key->matches(QKeySequence::Paste)
        || key->matches(QKeySequence::Cut);
    if (editsText) moveCursorToInput();
    return QWidget::eventFilter(watched, event);
}
