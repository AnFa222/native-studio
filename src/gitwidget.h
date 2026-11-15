#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class GitWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit GitWidget(QWidget *parent = nullptr);
    ~GitWidget() override;
    void setRepositoryPath(const QString &path);

signals:
    void fileActivated(const QString &path);
    void runInTerminalRequested(const QString &program, const QStringList &arguments,
                                const QString &workingDirectory);

private slots:
    void refresh();
    void showDiff(QTreeWidgetItem *item);
    void stageSelected();
    void unstageSelected();
    void commit();
    void signInToGitHub();
    void pull();
    void push();
    void restoreSelected();
    void stageAll();
    void unstageAll();
    void fetch();
    void showHistory();
    void stash();
    void stashPop();
    void initializeRepository();
    void createBranch();
    void switchBranch();
    void cloneRepository();
    void openOnGitHub();

private:
    void runGit(const QStringList &arguments, const std::function<void(const QString &)> &finished);
    void refreshGitHubStatus();
    [[nodiscard]] QString configuredGit() const;
    [[nodiscard]] QString selectedPath() const;

    QString m_rootPath;
    QLabel *m_branch = nullptr;
    QLabel *m_githubAccount = nullptr;
    QTreeWidget *m_files = nullptr;
    QPlainTextEdit *m_diff = nullptr;
    QLineEdit *m_message = nullptr;
    QToolButton *m_commit = nullptr;
};
