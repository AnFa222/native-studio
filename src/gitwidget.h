#pragma once

#include "extension_api.h"

#include <QWidget>
#include <QList>
#include <QPair>

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
    explicit GitWidget(const NsHostApi *hostApi, NsExtensionHandle extension,
                       QWidget *parent = nullptr);
    ~GitWidget() override;
    void setRepositoryPath(const QString &path);

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
    void publishDecorations(const QList<QPair<QString, uint32_t>> &decorations);
    void runInTerminal(const QString &program, const QStringList &arguments,
                       const QString &workingDirectory);
    [[nodiscard]] QString configuredGit() const;
    [[nodiscard]] QString selectedPath() const;

    QString m_rootPath;
    const NsHostApi *m_hostApi = nullptr;
    NsExtensionHandle m_extensionHandle = nullptr;
    QLabel *m_branch = nullptr;
    QLabel *m_githubAccount = nullptr;
    QTreeWidget *m_files = nullptr;
    QPlainTextEdit *m_diff = nullptr;
    QLabel *m_diffLabel = nullptr;
    QLineEdit *m_message = nullptr;
    QToolButton *m_commit = nullptr;
};
