#include "terminalwidget.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollBar>
#include <QVBoxLayout>

TerminalWidget::TerminalWidget(QWidget *parent)
    : QWidget(parent)
    , m_output(new QPlainTextEdit(this))
    , m_input(new QLineEdit(this))
    , m_shell(new QProcess(this))
    , m_runner(new QProcess(this))
    , m_workingDirectory(QDir::currentPath())
{
    const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_output->setObjectName(QStringLiteral("terminalOutput"));
    m_output->setReadOnly(true);
    m_output->setUndoRedoEnabled(false);
    m_output->setMaximumBlockCount(10000);
    m_output->setFont(font);
    m_input->setObjectName(QStringLiteral("terminalInput"));
    m_input->setFont(font);
    m_input->setPlaceholderText(tr("Enter a shell command"));

    auto *prompt = new QLabel(QStringLiteral(">"), this);
    prompt->setFont(font);
    auto *inputLayout = new QHBoxLayout;
    inputLayout->setContentsMargins(8, 3, 8, 6);
    inputLayout->addWidget(prompt);
    inputLayout->addWidget(m_input);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_output);
    layout->addLayout(inputLayout);

    m_shell->setProcessChannelMode(QProcess::MergedChannels);
    m_runner->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_input, &QLineEdit::returnPressed, this, &TerminalWidget::submitCommand);
    connect(m_shell, &QProcess::readyReadStandardOutput, this, &TerminalWidget::readShellOutput);
    connect(m_runner, &QProcess::readyReadStandardOutput, this, &TerminalWidget::readRunnerOutput);
    connect(m_runner, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        readRunnerOutput();
        appendMessage(tr("\n[Process exited with code %1]\n").arg(exitCode));
        emit commandFinished(exitCode);
    });
    connect(m_shell, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        appendMessage(tr("[Terminal error: %1]\n").arg(m_shell->errorString()));
    });
    connect(m_runner, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        appendMessage(tr("[Run error: %1]\n").arg(m_runner->errorString()));
    });
    startShell();
}

TerminalWidget::~TerminalWidget()
{
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
    m_shell->setWorkingDirectory(m_workingDirectory);
#ifdef Q_OS_WIN
    m_shell->start(QStringLiteral("cmd.exe"), {QStringLiteral("/Q")});
#else
    m_shell->start(QStringLiteral("/bin/sh"));
#endif
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

void TerminalWidget::submitCommand()
{
    const QString command = m_input->text();
    if (command.trimmed().isEmpty()) return;
    appendMessage(QStringLiteral("> %1\n").arg(command));
    m_shell->write((command +
#ifdef Q_OS_WIN
                    QStringLiteral("\r\n")
#else
                    QStringLiteral("\n")
#endif
                    ).toUtf8());
    m_input->clear();
}

void TerminalWidget::executeProgram(const QString &program, const QStringList &arguments,
                                    const QString &workingDirectory)
{
    if (m_runner->state() != QProcess::NotRunning) {
        appendMessage(tr("[A program is already running.]\n"));
        return;
    }
    const QString directory = workingDirectory.isEmpty() ? m_workingDirectory : workingDirectory;
    m_runner->setWorkingDirectory(directory);
    appendMessage(tr("\n[Running %1 %2]\n").arg(program, arguments.join(QLatin1Char(' '))));
    m_runner->start(program, arguments);
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
    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(message);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}
