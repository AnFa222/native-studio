#pragma once

#include <QByteArray>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QSpinBox;
class QToolButton;
class QTreeWidget;

class PythonDebuggerWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit PythonDebuggerWidget(QWidget *parent = nullptr);
    ~PythonDebuggerWidget() override;
    void setScriptPath(const QString &path);
    void setPathProvider(std::function<QString()> provider);
    void setLocationRevealer(std::function<void(const QString &, int)> revealer);

private slots:
    void startDebugging();
    void stopDebugging();
    void continueDebugging();
    void stepInto();
    void stepOver();
    void stepOut();
    void addBreakpoint();
    void removeBreakpoint();
    void readStandardOutput();
    void readStandardError();
    void processFinished();

private:
    void sendCommand(const QByteArray &command);
    void handleDebugMessage(const QByteArray &json);
    void setPaused(bool paused);
    QStringList breakpointArguments() const;

    QString m_scriptPath;
    std::function<QString()> m_pathProvider;
    std::function<void(const QString &, int)> m_locationRevealer;
    QByteArray m_stdoutBuffer;
    QLabel *m_fileLabel = nullptr;
    QLabel *m_status = nullptr;
    QTreeWidget *m_variables = nullptr;
    QTreeWidget *m_stack = nullptr;
    QListWidget *m_breakpoints = nullptr;
    QSpinBox *m_breakpointLine = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QToolButton *m_start = nullptr;
    QToolButton *m_continue = nullptr;
    QToolButton *m_stepInto = nullptr;
    QToolButton *m_stepOver = nullptr;
    QToolButton *m_stepOut = nullptr;
    QToolButton *m_stop = nullptr;
    QProcess *m_process = nullptr;
};
