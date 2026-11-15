#pragma once

#include <QWidget>

class QEvent;
class QPlainTextEdit;
class QProcess;

class TerminalWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr, const QString &shellProgram = {},
                            const QStringList &shellArguments = {});
    ~TerminalWidget() override;

    void setWorkingDirectory(const QString &directory);
    void startDeferred();
    void reloadSettings();
    void executeProgram(const QString &program, const QStringList &arguments,
                        const QString &workingDirectory = {});
    [[nodiscard]] QString outputText() const;
    [[nodiscard]] bool canTerminate() const;

public slots:
    void terminateActiveProcess();

signals:
    void commandFinished(int exitCode);
    void terminateAvailabilityChanged(bool available);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void submitCurrentInput();
    void readShellOutput();
    void readRunnerOutput();
    void updateTerminateButton();

private:
    void startShell();
    void appendOutput(const QByteArray &bytes);
    void appendMessage(const QString &message);
    [[nodiscard]] QString currentInput() const;
    void replaceCurrentInput(const QString &text);
    void moveCursorToInput();

    QPlainTextEdit *m_output = nullptr;
    QProcess *m_shell = nullptr;
    QProcess *m_runner = nullptr;
    QString m_workingDirectory;
    QStringList m_history;
    int m_inputStart = 0;
    int m_historyIndex = 0;
    bool m_shellStarted = false;
    bool m_shellTerminationRequested = false;
    bool m_runnerTerminationRequested = false;
    QString m_shellProgram;
    QStringList m_shellArguments;
};
