#include "gitwidget.h"

#include <QAction>
#include <QDir>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum class GitIcon {
    Refresh, Pull, Push, Branch, Stage, Unstage, Restore, StageAll, UnstageAll, Commit, More
};

class GitToolButton final : public QToolButton
{
public:
    GitToolButton(GitIcon icon, const QString &label, QWidget *parent)
        : QToolButton(parent), m_icon(icon)
    {
        setToolTip(label);
        setAccessibleName(label);
        setAutoRaise(true);
        setFixedSize(26, 26);
        setCursor(Qt::ArrowCursor);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QToolButton::paintEvent(event);
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor color = palette().color(isEnabled() ? QPalette::Active
                                                    : QPalette::Disabled,
                                       QPalette::ButtonText);
        QPen pen(color, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QPointF c = rect().center();
        const auto line = [&painter](qreal x1, qreal y1, qreal x2, qreal y2) {
            painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        };
        switch (m_icon) {
        case GitIcon::Refresh:
            painter.drawArc(QRectF(c.x() - 6, c.y() - 6, 12, 12), 35 * 16, 285 * 16);
            line(c.x() + 4, c.y() - 6, c.x() + 7, c.y() - 6);
            line(c.x() + 7, c.y() - 6, c.x() + 7, c.y() - 3);
            break;
        case GitIcon::Pull:
        case GitIcon::Push: {
            const bool up = m_icon == GitIcon::Push;
            const qreal tip = c.y() + (up ? -6 : 6);
            const qreal tail = c.y() + (up ? 4 : -4);
            line(c.x(), tail, c.x(), tip);
            line(c.x(), tip, c.x() - 3.5, tip + (up ? 3.5 : -3.5));
            line(c.x(), tip, c.x() + 3.5, tip + (up ? 3.5 : -3.5));
            line(c.x() - 6, c.y() + (up ? 6 : -6), c.x() + 6, c.y() + (up ? 6 : -6));
            break;
        }
        case GitIcon::Branch:
            painter.drawEllipse(QPointF(c.x() - 4, c.y() - 5), 1.7, 1.7);
            painter.drawEllipse(QPointF(c.x() + 4, c.y() - 1), 1.7, 1.7);
            painter.drawEllipse(QPointF(c.x() - 4, c.y() + 5), 1.7, 1.7);
            line(c.x() - 4, c.y() - 3.2, c.x() - 4, c.y() + 3.2);
            painter.drawPath([&] { QPainterPath p; p.moveTo(c.x() - 4, c.y() + 1);
                p.cubicTo(c.x() - 4, c.y() - 1, c.x() + 4, c.y() + 1, c.x() + 4, c.y() - 1); return p; }());
            break;
        case GitIcon::Stage:
        case GitIcon::Unstage:
            line(c.x() - 5, c.y(), c.x() + 5, c.y());
            if (m_icon == GitIcon::Stage) line(c.x(), c.y() - 5, c.x(), c.y() + 5);
            break;
        case GitIcon::Restore:
            painter.drawArc(QRectF(c.x() - 5, c.y() - 5, 11, 11), 65 * 16, 255 * 16);
            line(c.x() - 6, c.y() - 5, c.x() - 6, c.y());
            line(c.x() - 6, c.y() - 5, c.x() - 1, c.y() - 5);
            break;
        case GitIcon::StageAll:
        case GitIcon::UnstageAll:
            line(c.x() - 6, c.y() - 4, c.x() + 2, c.y() - 4);
            line(c.x() - 6, c.y(), c.x() + 2, c.y());
            line(c.x() - 6, c.y() + 4, c.x() + 2, c.y() + 4);
            line(c.x() + 5, c.y() - 3, c.x() + 5, c.y() + 3);
            if (m_icon == GitIcon::StageAll) line(c.x() + 2, c.y(), c.x() + 8, c.y());
            break;
        case GitIcon::Commit:
            line(c.x() - 6, c.y(), c.x() - 2, c.y() + 4);
            line(c.x() - 2, c.y() + 4, c.x() + 6, c.y() - 5);
            break;
        case GitIcon::More:
            painter.setBrush(color);
            for (int offset : {-5, 0, 5}) painter.drawEllipse(QPointF(c.x() + offset, c.y()), 1.2, 1.2);
            break;
        }
    }

private:
    GitIcon m_icon;
};

GitToolButton *makeGitButton(QWidget *parent, GitIcon icon, const QString &label,
                             const char *objectName)
{
    auto *button = new GitToolButton(icon, label, parent);
    button->setObjectName(QString::fromLatin1(objectName));
    return button;
}

QAction *addRepositoryAction(QMenu *menu, const QString &text, const char *objectName)
{
    QAction *action = menu->addAction(text);
    action->setObjectName(QString::fromLatin1(objectName));
    return action;
}
}

GitWidget::GitWidget(QWidget *parent)
    : QWidget(parent), m_branch(new QLabel(tr("No repository"), this))
    , m_githubAccount(new QLabel(tr("GitHub: Not signed in"), this))
    , m_files(new QTreeWidget(this)), m_diff(new QPlainTextEdit(this))
    , m_message(new QLineEdit(this))
    , m_commit(makeGitButton(this, GitIcon::Commit, tr("Commit staged changes"), "gitCommit"))
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
    m_message->setClearButtonEnabled(true);
    auto *refreshButton = makeGitButton(this, GitIcon::Refresh, tr("Refresh source control"), "gitRefresh");
    auto *pullButton = makeGitButton(this, GitIcon::Pull, tr("Pull from remote"), "gitPull");
    auto *pushButton = makeGitButton(this, GitIcon::Push, tr("Push to remote"), "gitPush");
    auto *switchBranchButton = makeGitButton(this, GitIcon::Branch, tr("Switch branch"), "gitSwitchBranch");
    auto *stageButton = makeGitButton(this, GitIcon::Stage, tr("Stage selected file"), "gitStage");
    auto *unstageButton = makeGitButton(this, GitIcon::Unstage, tr("Unstage selected file"), "gitUnstage");
    auto *restoreButton = makeGitButton(this, GitIcon::Restore, tr("Discard changes in selected file"), "gitRestore");
    auto *stageAllButton = makeGitButton(this, GitIcon::StageAll, tr("Stage all changes"), "gitStageAll");
    auto *unstageAllButton = makeGitButton(this, GitIcon::UnstageAll, tr("Unstage all changes"), "gitUnstageAll");
    auto *moreButton = makeGitButton(this, GitIcon::More, tr("More Git actions"), "gitMoreActions");
    auto *moreMenu = new QMenu(moreButton);
    QAction *fetchAction = addRepositoryAction(moreMenu, tr("Fetch from Remote"), "gitFetch");
    QAction *historyAction = addRepositoryAction(moreMenu, tr("View History"), "gitHistory");
    moreMenu->addSeparator();
    QAction *stashAction = addRepositoryAction(moreMenu, tr("Stash Changes"), "gitStash");
    QAction *stashPopAction = addRepositoryAction(moreMenu, tr("Pop Latest Stash"), "gitStashPop");
    moreMenu->addSeparator();
    QAction *createBranchAction = addRepositoryAction(moreMenu, tr("Create Branch…"), "gitCreateBranch");
    QAction *initAction = addRepositoryAction(moreMenu, tr("Initialize Repository"), "gitInit");
    QAction *cloneAction = addRepositoryAction(moreMenu, tr("Clone Repository…"), "gitClone");
    moreMenu->addSeparator();
    QAction *signInAction = addRepositoryAction(moreMenu, tr("Sign in to GitHub"), "githubSignIn");
    QAction *openGitHubAction = addRepositoryAction(moreMenu, tr("Open Repository on GitHub"), "gitOpenGitHub");
    moreButton->setMenu(moreMenu);
    moreButton->setPopupMode(QToolButton::InstantPopup);

    auto *top = new QHBoxLayout;
    top->setContentsMargins(2, 0, 0, 0);
    top->setSpacing(1);
    top->addWidget(m_branch, 1);
    top->addWidget(switchBranchButton);
    top->addWidget(pullButton);
    top->addWidget(pushButton);
    top->addWidget(refreshButton);
    top->addWidget(moreButton);
    m_githubAccount->setObjectName(QStringLiteral("gitAccountStatus"));
    m_githubAccount->setContentsMargins(4, 0, 0, 2);
    auto *changes = new QHBoxLayout;
    changes->setContentsMargins(4, 2, 0, 0);
    changes->setSpacing(1);
    auto *changesLabel = new QLabel(tr("CHANGES"), this);
    changesLabel->setObjectName(QStringLiteral("gitSectionLabel"));
    changes->addWidget(changesLabel);
    changes->addStretch();
    changes->addWidget(stageButton);
    changes->addWidget(unstageButton);
    changes->addWidget(restoreButton);
    changes->addWidget(stageAllButton);
    changes->addWidget(unstageAllButton);
    auto *commitRow = new QHBoxLayout;
    commitRow->setSpacing(2);
    commitRow->addWidget(m_message, 1);
    commitRow->addWidget(m_commit);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    layout->addLayout(top);
    layout->addWidget(m_githubAccount);
    layout->addLayout(changes);
    layout->addWidget(m_files, 1);
    layout->addWidget(m_diff, 1);
    layout->addLayout(commitRow);
    connect(refreshButton, &QToolButton::clicked, this, &GitWidget::refresh);
    connect(pullButton, &QToolButton::clicked, this, &GitWidget::pull);
    connect(pushButton, &QToolButton::clicked, this, &GitWidget::push);
    connect(switchBranchButton, &QToolButton::clicked, this, &GitWidget::switchBranch);
    connect(stageButton, &QToolButton::clicked, this, &GitWidget::stageSelected);
    connect(unstageButton, &QToolButton::clicked, this, &GitWidget::unstageSelected);
    connect(restoreButton, &QToolButton::clicked, this, &GitWidget::restoreSelected);
    connect(stageAllButton, &QToolButton::clicked, this, &GitWidget::stageAll);
    connect(unstageAllButton, &QToolButton::clicked, this, &GitWidget::unstageAll);
    connect(fetchAction, &QAction::triggered, this, &GitWidget::fetch);
    connect(historyAction, &QAction::triggered, this, &GitWidget::showHistory);
    connect(stashAction, &QAction::triggered, this, &GitWidget::stash);
    connect(stashPopAction, &QAction::triggered, this, &GitWidget::stashPop);
    connect(createBranchAction, &QAction::triggered, this, &GitWidget::createBranch);
    connect(initAction, &QAction::triggered, this, &GitWidget::initializeRepository);
    connect(cloneAction, &QAction::triggered, this, &GitWidget::cloneRepository);
    connect(signInAction, &QAction::triggered, this, &GitWidget::signInToGitHub);
    connect(openGitHubAction, &QAction::triggered, this, &GitWidget::openOnGitHub);
    connect(m_commit, &QToolButton::clicked, this, &GitWidget::commit);
    connect(m_message, &QLineEdit::returnPressed, this, &GitWidget::commit);
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
