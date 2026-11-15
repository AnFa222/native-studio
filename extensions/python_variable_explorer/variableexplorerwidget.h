#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QTreeWidget;

class VariableExplorerWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit VariableExplorerWidget(QWidget *parent = nullptr);
    void setScriptPath(const QString &path);

private slots:
    void runScript();
    void stopScript();
    void processFinished();

private:
    void showVariables(const QByteArray &json);

    QString m_scriptPath;
    QLabel *m_fileLabel = nullptr;
    QLabel *m_status = nullptr;
    QTreeWidget *m_variables = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QPushButton *m_run = nullptr;
    QPushButton *m_stop = nullptr;
    QProcess *m_process = nullptr;
};
