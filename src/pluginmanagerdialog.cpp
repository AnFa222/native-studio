#include "pluginmanagerdialog.h"

#include "languagemanager.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int FileNameRole = Qt::UserRole + 1;
constexpr int DownloadUrlRole = Qt::UserRole + 2;
constexpr int BundledRole = Qt::UserRole + 3;
constexpr int FilePathRole = Qt::UserRole + 4;

void applyGitHubHeaders(QNetworkRequest &request)
{
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "Native-Studio-Plugin-Manager");
    const QByteArray token = qgetenv("NATIVE_STUDIO_GITHUB_TOKEN");
    if (!token.isEmpty()) request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
}
}

PluginManagerDialog::PluginManagerDialog(LanguageManager *languageManager, QWidget *parent)
    : QDialog(parent)
    , m_languageManager(languageManager)
    , m_network(new QNetworkAccessManager(this))
    , m_repository(new QLineEdit(this))
    , m_table(new QTableWidget(this))
    , m_status(new QLabel(this))
{
    setWindowTitle(tr("Manage Language Plugins"));
    resize(760, 460);
    setModal(true);

    m_repository->setPlaceholderText(tr("owner/repository"));
    m_repository->setText(QSettings().value(QStringLiteral("plugins/githubRepository"),
                                             QStringLiteral("AnFa222/native-studio")).toString());
    m_repository->setToolTip(tr("GitHub repository whose latest release contains language_*.dll assets"));

    auto *checkButton = new QPushButton(tr("Check GitHub"), this);
    auto *repositoryLayout = new QHBoxLayout;
    repositoryLayout->addWidget(new QLabel(tr("Plugin repository:"), this));
    repositoryLayout->addWidget(m_repository, 1);
    repositoryLayout->addWidget(checkButton);

    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("Plugin"), tr("Extensions"), tr("Installed"),
                                        tr("Available"), tr("Source")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    auto *installButton = new QPushButton(tr("Install / Update"), this);
    auto *removeButton = new QPushButton(tr("Remove"), this);
    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *actions = new QHBoxLayout;
    actions->addWidget(installButton);
    actions->addWidget(removeButton);
    actions->addStretch();
    actions->addWidget(closeButtons);

    m_status->setWordWrap(true);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(repositoryLayout);
    layout->addWidget(new QLabel(
        tr("Installable plugins are Windows DLL assets named language_*.dll in the latest GitHub release. "
           "For private repositories, set NATIVE_STUDIO_GITHUB_TOKEN before starting the IDE."), this));
    layout->addWidget(m_table, 1);
    layout->addWidget(m_status);
    layout->addLayout(actions);

    connect(checkButton, &QPushButton::clicked, this, &PluginManagerDialog::checkGitHub);
    connect(installButton, &QPushButton::clicked, this, &PluginManagerDialog::installOrUpdateSelected);
    connect(removeButton, &QPushButton::clicked, this, &PluginManagerDialog::removeSelected);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refreshTable();
}

void PluginManagerDialog::refreshTable()
{
    const auto installed = m_languageManager->pluginInfos();
    QStringList fileNames;
    for (const auto &info : installed) fileNames.push_back(QFileInfo(info.filePath).fileName());
    for (const auto &remote : m_remotePlugins) {
        if (!fileNames.contains(remote.fileName, Qt::CaseInsensitive)) fileNames.push_back(remote.fileName);
    }

    m_table->setRowCount(fileNames.size());
    for (int row = 0; row < fileNames.size(); ++row) {
        const QString fileName = fileNames.at(row);
        const LanguagePluginInfo *local = nullptr;
        const RemotePlugin *remote = nullptr;
        for (const auto &info : installed) {
            if (QFileInfo(info.filePath).fileName().compare(fileName, Qt::CaseInsensitive) == 0) {
                local = &info;
                break;
            }
        }
        for (const auto &candidate : m_remotePlugins) {
            if (candidate.fileName.compare(fileName, Qt::CaseInsensitive) == 0) {
                remote = &candidate;
                break;
            }
        }

        const QString displayName = local ? local->name : QFileInfo(fileName).completeBaseName();
        auto *nameItem = new QTableWidgetItem(displayName);
        nameItem->setData(FileNameRole, fileName);
        nameItem->setData(DownloadUrlRole, remote ? remote->downloadUrl : QString{});
        nameItem->setData(BundledRole, local ? local->bundled : false);
        nameItem->setData(FilePathRole, local ? local->filePath : QString{});
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, new QTableWidgetItem(local ? local->extensions.join(QStringLiteral(", ")) : QString{}));
        m_table->setItem(row, 2, new QTableWidgetItem(local ? local->version : tr("Not installed")));
        m_table->setItem(row, 3, new QTableWidgetItem(remote ? remote->version : QStringLiteral("—")));
        m_table->setItem(row, 4, new QTableWidgetItem(local ? (local->bundled ? tr("Bundled") : tr("User")) : tr("GitHub")));
    }
    if (m_table->rowCount() > 0) m_table->selectRow(0);
}

void PluginManagerDialog::checkGitHub()
{
    const QString repository = m_repository->text().trimmed();
    const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"));
    if (!valid.match(repository).hasMatch()) {
        setStatus(tr("Enter a GitHub repository as owner/repository."), true);
        return;
    }
    QSettings().setValue(QStringLiteral("plugins/githubRepository"), repository);
    setStatus(tr("Checking the latest GitHub release…"));

    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repository)));
    applyGitHubHeaders(request);
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(tr("GitHub request failed: %1").arg(reply->errorString()), true);
            reply->deleteLater();
            return;
        }
        const auto release = QJsonDocument::fromJson(payload).object();
        const QString version = release.value(QStringLiteral("tag_name")).toString();
        m_remotePlugins.clear();
        for (const auto &value : release.value(QStringLiteral("assets")).toArray()) {
            const auto asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            if (!name.startsWith(QStringLiteral("language_"), Qt::CaseInsensitive)
                || !name.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive)) continue;
            m_remotePlugins.push_back({name, version,
                asset.value(QStringLiteral("browser_download_url")).toString()});
        }
        refreshTable();
        setStatus(m_remotePlugins.isEmpty()
            ? tr("The latest release has no language_*.dll assets.")
            : tr("Found %1 plugin asset(s) in release %2.").arg(m_remotePlugins.size()).arg(version));
        reply->deleteLater();
    });
}

int PluginManagerDialog::selectedRow() const
{
    return m_table->selectionModel()->selectedRows().isEmpty()
        ? -1 : m_table->selectionModel()->selectedRows().first().row();
}

void PluginManagerDialog::installOrUpdateSelected()
{
    const int row = selectedRow();
    if (row < 0) return;
    const auto *item = m_table->item(row, 0);
    const QString url = item->data(DownloadUrlRole).toString();
    const QString fileName = item->data(FileNameRole).toString();
    if (url.isEmpty()) {
        setStatus(tr("Check GitHub first; no downloadable asset is available for this plugin."), true);
        return;
    }

    setStatus(tr("Downloading %1…").arg(fileName));
    QNetworkRequest request{QUrl(url)};
    applyGitHubHeaders(request);
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fileName] {
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(tr("Download failed: %1").arg(reply->errorString()), true);
            reply->deleteLater();
            return;
        }
        QDir directory(LanguageManager::userPluginDirectory());
        directory.mkpath(QStringLiteral("."));
        QSaveFile file(directory.filePath(fileName + QStringLiteral(".pending")));
        if (!file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0 || !file.commit()) {
            setStatus(tr("Could not stage the downloaded plugin."), true);
            reply->deleteLater();
            return;
        }
        QSettings settings;
        auto disabled = settings.value(QStringLiteral("plugins/disabled")).toStringList();
        disabled.removeAll(fileName);
        settings.setValue(QStringLiteral("plugins/disabled"), disabled);
        setStatus(tr("%1 is staged. Restart Native Studio to finish installation.").arg(fileName));
        reply->deleteLater();
    });
}

void PluginManagerDialog::removeSelected()
{
    const int row = selectedRow();
    if (row < 0) return;
    const auto *item = m_table->item(row, 0);
    const QString fileName = item->data(FileNameRole).toString();
    const QString filePath = item->data(FilePathRole).toString();
    if (filePath.isEmpty()) {
        setStatus(tr("This plugin is not installed."), true);
        return;
    }
    if (QMessageBox::question(this, tr("Remove Plugin"),
        tr("Remove %1 after Native Studio restarts?").arg(item->text())) != QMessageBox::Yes) return;

    QSettings settings;
    auto disabled = settings.value(QStringLiteral("plugins/disabled")).toStringList();
    if (!disabled.contains(fileName, Qt::CaseInsensitive)) disabled.push_back(fileName);
    settings.setValue(QStringLiteral("plugins/disabled"), disabled);
    if (!item->data(BundledRole).toBool()) {
        QDir directory(LanguageManager::userPluginDirectory());
        directory.mkpath(QStringLiteral("."));
        QFile marker(directory.filePath(fileName + QStringLiteral(".remove")));
        marker.open(QIODevice::WriteOnly);
    }
    setStatus(tr("%1 will be removed or disabled after restart.").arg(item->text()));
}

void PluginManagerDialog::setStatus(const QString &message, bool error)
{
    m_status->setText(message);
    m_status->setStyleSheet(error ? QStringLiteral("color: #f48771;") : QStringLiteral("color: #cccccc;"));
}
