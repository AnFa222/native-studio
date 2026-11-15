#pragma once

#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QProcess;

class TerminalWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    void setWorkingDirectory(const QString &directory);
    void startDeferred();
    void executeProgram(const QString &program, const QStringList &arguments,
                        const QString &workingDirectory = {});
    [[nodiscard]] QString outputText() const;

signals:
    void commandFinished(int exitCode);

private slots:
    void submitCommand();
    void readShellOutput();
    void readRunnerOutput();

private:
    void startShell();
    void appendOutput(const QByteArray &bytes);
    void appendMessage(const QString &message);

    QPlainTextEdit *m_output = nullptr;
    QLineEdit *m_input = nullptr;
    QProcess *m_shell = nullptr;
    QProcess *m_runner = nullptr;
    QString m_workingDirectory;
    bool m_shellStarted = false;
};
