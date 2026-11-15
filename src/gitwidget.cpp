#include "gitwidget.h"

#include <QDir>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

GitWidget::GitWidget(QWidget *parent)
    : QWidget(parent), m_branch(new QLabel(tr("No repository"), this))
    , m_githubAccount(new QLabel(tr("GitHub: Not signed in"), this))
    , m_files(new QTreeWidget(this)), m_diff(new QPlainTextEdit(this))
    , m_message(new QLineEdit(this)), m_commit(new QPushButton(tr("Commit"), this))
{
    setObjectName(QStringLiteral("gitWidget"));
    m_branch->setObjectName(QStringLiteral("gitBranch"));
    m_githubAccount->setObjectName(QStringLiteral("githubAccountStatus"));
    m_files->setObjectName(QStringLiteral("gitChanges"));
    m_diff->setObjectName(QStringLiteral("gitDiff"));
    m_message->setObjectName(QStringLiteral("gitCommitMessage"));
    m_files->setHeaderLabels({tr("Status"), tr("File")});
    m_diff->setReadOnly(true);
    m_message->setPlaceholderText(tr("Commit message"));
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    auto *signInButton = new QPushButton(tr("Sign in to GitHub"), this);
    auto *pullButton = new QPushButton(tr("Pull"), this);
    auto *pushButton = new QPushButton(tr("Push"), this);
    signInButton->setObjectName(QStringLiteral("githubSignIn"));
    pullButton->setObjectName(QStringLiteral("gitPull"));
    pushButton->setObjectName(QStringLiteral("gitPush"));
    auto *stageButton = new QPushButton(tr("Stage"), this);
    auto *unstageButton = new QPushButton(tr("Unstage"), this);
    auto *restoreButton = new QPushButton(tr("Restore"), this);
    auto *stageAllButton = new QPushButton(tr("Stage All"), this);
    auto *unstageAllButton = new QPushButton(tr("Unstage All"), this);
    restoreButton->setObjectName(QStringLiteral("gitRestore"));
    stageAllButton->setObjectName(QStringLiteral("gitStageAll"));
    unstageAllButton->setObjectName(QStringLiteral("gitUnstageAll"));
    auto *top = new QHBoxLayout;
    top->addWidget(m_branch, 1);
    top->addWidget(refreshButton);
    auto *remoteActions = new QHBoxLayout;
    remoteActions->addWidget(m_githubAccount);
    remoteActions->addStretch();
    remoteActions->addWidget(signInButton);
    remoteActions->addWidget(pullButton);
    remoteActions->addWidget(pushButton);
    auto *actions = new QHBoxLayout;
    actions->addWidget(stageButton);
    actions->addWidget(unstageButton);
    actions->addWidget(restoreButton);
    actions->addWidget(stageAllButton);
    actions->addWidget(unstageAllButton);
    actions->addStretch();
    auto *repositoryActions = new QGridLayout;
    const QList<QPair<QString, const char *>> repositoryButtons{
        {tr("Fetch"), "gitFetch"}, {tr("History"), "gitHistory"},
        {tr("Stash"), "gitStash"}, {tr("Pop Stash"), "gitStashPop"},
        {tr("New Branch"), "gitCreateBranch"}, {tr("Switch Branch"), "gitSwitchBranch"},
        {tr("Init"), "gitInit"}, {tr("Clone"), "gitClone"},
        {tr("Open on GitHub"), "gitOpenGitHub"}
    };
    QList<QPushButton *> extraButtons;
    for (const auto &[text, name] : repositoryButtons) {
        auto *button = new QPushButton(text, this);
        button->setObjectName(QString::fromLatin1(name));
        const int index = extraButtons.size();
        repositoryActions->addWidget(button, index / 3, index % 3);
        extraButtons.push_back(button);
    }
    auto *commitRow = new QHBoxLayout;
    commitRow->addWidget(m_message, 1);
    commitRow->addWidget(m_commit);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(top);
    layout->addLayout(remoteActions);
    layout->addWidget(m_files, 1);
    layout->addLayout(actions);
    layout->addLayout(repositoryActions);
    layout->addWidget(m_diff, 1);
    layout->addLayout(commitRow);
    connect(refreshButton, &QPushButton::clicked, this, &GitWidget::refresh);
    connect(signInButton, &QPushButton::clicked, this, &GitWidget::signInToGitHub);
    connect(pullButton, &QPushButton::clicked, this, &GitWidget::pull);
    connect(pushButton, &QPushButton::clicked, this, &GitWidget::push);
    connect(stageButton, &QPushButton::clicked, this, &GitWidget::stageSelected);
    connect(unstageButton, &QPushButton::clicked, this, &GitWidget::unstageSelected);
    connect(restoreButton, &QPushButton::clicked, this, &GitWidget::restoreSelected);
    connect(stageAllButton, &QPushButton::clicked, this, &GitWidget::stageAll);
    connect(unstageAllButton, &QPushButton::clicked, this, &GitWidget::unstageAll);
    connect(extraButtons.at(0), &QPushButton::clicked, this, &GitWidget::fetch);
    connect(extraButtons.at(1), &QPushButton::clicked, this, &GitWidget::showHistory);
    connect(extraButtons.at(2), &QPushButton::clicked, this, &GitWidget::stash);
    connect(extraButtons.at(3), &QPushButton::clicked, this, &GitWidget::stashPop);
    connect(extraButtons.at(4), &QPushButton::clicked, this, &GitWidget::createBranch);
    connect(extraButtons.at(5), &QPushButton::clicked, this, &GitWidget::switchBranch);
    connect(extraButtons.at(6), &QPushButton::clicked, this, &GitWidget::initializeRepository);
    connect(extraButtons.at(7), &QPushButton::clicked, this, &GitWidget::cloneRepository);
    connect(extraButtons.at(8), &QPushButton::clicked, this, &GitWidget::openOnGitHub);
    connect(m_commit, &QPushButton::clicked, this, &GitWidget::commit);
    connect(m_files, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) { showDiff(item); });
    connect(m_files, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (item) emit fileActivated(QDir(m_rootPath).filePath(item->data(0, Qt::UserRole).toString()));
    });
}

GitWidget::~GitWidget()
{
    const auto processes = findChildren<QProcess *>(QString{}, Qt::FindDirectChildrenOnly);
    for (QProcess *process : processes) {
        disconnect(process, nullptr, this, nullptr);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(500);
        }
    }
}

void GitWidget::setRepositoryPath(const QString &path)
{
    m_rootPath = QFileInfo(path).absoluteFilePath();
    refresh();
}

void GitWidget::runGit(const QStringList &arguments,
                       const std::function<void(const QString &)> &finished)
{
    if (m_rootPath.isEmpty()) return;
    auto *process = new QProcess(this);
    process->setProgram(configuredGit());
    process->setArguments(QStringList{QStringLiteral("-C"), m_rootPath} + arguments);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [process, finished](int, QProcess::ExitStatus) {
        const QString output = QString::fromUtf8(process->readAll());
        process->deleteLater();
        finished(output);
    });
    connect(process, &QProcess::errorOccurred, this,
            [process, finished](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        const QString output = process->errorString();
        process->deleteLater();
        finished(output);
    });
    process->start();
}

QString GitWidget::configuredGit() const
{
    const QString value = QSettings().value(
        QStringLiteral("extensions/settings/dev.nativestudio.git/git_path"),
        QStringLiteral("git")).toString().trimmed();
    return value.isEmpty() ? QStringLiteral("git") : value;
}

void GitWidget::refresh()
{
    m_files->clear();
    m_diff->clear();
    refreshGitHubStatus();
    runGit({QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("--branch")},
           [this](const QString &output) {
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.isEmpty() || !lines.first().startsWith(QStringLiteral("##"))) {
            m_branch->setText(tr("Not a Git repository"));
            return;
        }
        m_branch->setText(lines.first().mid(3));
        for (int index = 1; index < lines.size(); ++index) {
            const QString line = lines.at(index);
            if (line.size() < 4) continue;
            const QString status = line.left(2);
            QString path = line.mid(3);
            const int arrow = path.indexOf(QStringLiteral(" -> "));
            if (arrow >= 0) path = path.mid(arrow + 4);
            auto *item = new QTreeWidgetItem(m_files);
            item->setText(0, status);
            item->setText(1, path);
            item->setData(0, Qt::UserRole, path);
        }
    });
}

void GitWidget::refreshGitHubStatus()
{
    auto *process = new QProcess(this);
    process->setProgram(configuredGit());
    process->setArguments({QStringLiteral("credential-manager"), QStringLiteral("github"),
                           QStringLiteral("list")});
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus) {
        const bool hasAccount = exitCode == 0
            && !process->readAllStandardOutput().trimmed().isEmpty();
        m_githubAccount->setText(hasAccount ? tr("GitHub: Signed in")
                                            : tr("GitHub: Not signed in"));
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_githubAccount->setText(tr("Git Credential Manager unavailable"));
        process->deleteLater();
    });
    process->start();
}

void GitWidget::signInToGitHub()
{
    emit runInTerminalRequested(configuredGit(),
        {QStringLiteral("credential-manager"), QStringLiteral("github"),
         QStringLiteral("login")}, m_rootPath);
}

void GitWidget::pull()
{
    if (m_rootPath.isEmpty()) return;
    emit runInTerminalRequested(configuredGit(),
        {QStringLiteral("-C"), m_rootPath, QStringLiteral("pull"), QStringLiteral("--ff-only")},
        m_rootPath);
}

void GitWidget::push()
{
    if (m_rootPath.isEmpty()) return;
    emit runInTerminalRequested(configuredGit(),
        {QStringLiteral("-C"), m_rootPath, QStringLiteral("push")}, m_rootPath);
}

void GitWidget::restoreSelected()
{
    const QString path = selectedPath();
    if (path.isEmpty()) return;
    if (m_files->currentItem()->text(0).contains(QLatin1Char('?'))) {
        QMessageBox::information(this, tr("Cannot Restore Untracked File"),
            tr("Git has no committed version of this untracked file."));
        return;
    }
    if (QMessageBox::warning(this, tr("Restore File"),
            tr("Discard changes and restore %1 from HEAD?").arg(path),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) return;
    runGit({QStringLiteral("restore"), QStringLiteral("--source=HEAD"),
            QStringLiteral("--staged"), QStringLiteral("--worktree"),
            QStringLiteral("--"), path}, [this](const QString &output) {
        if (!output.trimmed().isEmpty()) m_diff->setPlainText(output);
        refresh();
    });
}

void GitWidget::stageAll()
{
    runGit({QStringLiteral("add"), QStringLiteral("-A")}, [this](const QString &) { refresh(); });
}

void GitWidget::unstageAll()
{
    runGit({QStringLiteral("reset")}, [this](const QString &output) {
        if (!output.trimmed().isEmpty()) m_diff->setPlainText(output);
        refresh();
    });
}

void GitWidget::fetch()
{
    runGit({QStringLiteral("fetch"), QStringLiteral("--prune")},
           [this](const QString &output) { m_diff->setPlainText(output); refresh(); });
}

void GitWidget::showHistory()
{
    runGit({QStringLiteral("log"), QStringLiteral("--oneline"),
            QStringLiteral("--decorate"), QStringLiteral("--graph"),
            QStringLiteral("-n"), QStringLiteral("100")},
           [this](const QString &output) { m_diff->setPlainText(output); });
}

void GitWidget::stash()
{
    runGit({QStringLiteral("stash"), QStringLiteral("push"), QStringLiteral("-u")},
           [this](const QString &output) { m_diff->setPlainText(output); refresh(); });
}

void GitWidget::stashPop()
{
    runGit({QStringLiteral("stash"), QStringLiteral("pop")},
           [this](const QString &output) { m_diff->setPlainText(output); refresh(); });
}

void GitWidget::initializeRepository()
{
    runGit({QStringLiteral("init")}, [this](const QString &output) {
        m_diff->setPlainText(output);
        refresh();
    });
}

void GitWidget::createBranch()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Create Branch"), tr("Branch name:"),
                                                QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    runGit({QStringLiteral("switch"), QStringLiteral("-c"), name},
           [this](const QString &output) { m_diff->setPlainText(output); refresh(); });
}

void GitWidget::switchBranch()
{
    runGit({QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname:short)"),
            QStringLiteral("refs/heads")}, [this](const QString &output) {
        const QStringList branches = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (branches.isEmpty()) return;
        bool accepted = false;
        const QString branch = QInputDialog::getItem(this, tr("Switch Branch"), tr("Branch:"),
                                                      branches, 0, false, &accepted);
        if (!accepted || branch.isEmpty()) return;
        runGit({QStringLiteral("switch"), branch},
               [this](const QString &result) { m_diff->setPlainText(result); refresh(); });
    });
}

void GitWidget::cloneRepository()
{
    bool accepted = false;
    const QString remote = QInputDialog::getText(this, tr("Clone Repository"),
        tr("Repository URL:"), QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || remote.isEmpty()) return;
    const QString parent = QFileDialog::getExistingDirectory(this, tr("Clone Into Folder"),
                                                               m_rootPath);
    if (parent.isEmpty()) return;
    emit runInTerminalRequested(configuredGit(),
        {QStringLiteral("clone"), remote}, parent);
}

void GitWidget::openOnGitHub()
{
    runGit({QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")},
           [this](const QString &output) {
        QString url = output.trimmed();
        if (url.startsWith(QStringLiteral("git@github.com:")))
            url = QStringLiteral("https://github.com/") + url.mid(15);
        if (url.endsWith(QStringLiteral(".git"))) url.chop(4);
        if (url.startsWith(QStringLiteral("https://github.com/")))
            QDesktopServices::openUrl(QUrl(url));
        else
            m_diff->setPlainText(tr("The origin remote is not a GitHub URL."));
    });
}

QString GitWidget::selectedPath() const
{
    auto *item = m_files->currentItem();
    return item ? item->data(0, Qt::UserRole).toString() : QString{};
}

void GitWidget::showDiff(QTreeWidgetItem *item)
{
    if (!item) return;
    const QString path = item->data(0, Qt::UserRole).toString();
    runGit({QStringLiteral("diff"), QStringLiteral("--"), path},
           [this](const QString &output) { m_diff->setPlainText(output); });
}

void GitWidget::stageSelected()
{
    const QString path = selectedPath();
    if (path.isEmpty()) return;
    runGit({QStringLiteral("add"), QStringLiteral("--"), path}, [this](const QString &) { refresh(); });
}

void GitWidget::unstageSelected()
{
    const QString path = selectedPath();
    if (path.isEmpty()) return;
    runGit({QStringLiteral("restore"), QStringLiteral("--staged"), QStringLiteral("--"), path},
           [this](const QString &) { refresh(); });
}

void GitWidget::commit()
{
    const QString message = m_message->text().trimmed();
    if (message.isEmpty()) return;
    runGit({QStringLiteral("commit"), QStringLiteral("-m"), message}, [this](const QString &output) {
        m_diff->setPlainText(output);
        m_message->clear();
        refresh();
    });
}
