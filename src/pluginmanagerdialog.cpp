#include "pluginmanagerdialog.h"

#include "languagemanager.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
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
#include <QSaveFile>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>
#include <QVBoxLayout>

namespace {
constexpr int FileNameRole = Qt::UserRole + 1;
constexpr int DownloadUrlRole = Qt::UserRole + 2;
constexpr int BundledRole = Qt::UserRole + 3;
constexpr int FilePathRole = Qt::UserRole + 4;
constexpr int RemoteVersionRole = Qt::UserRole + 5;
constexpr int StatusRole = Qt::UserRole + 6;
constexpr auto PluginRepository = "AnFa222/native-studio";

QString displayNameForAsset(const QString &fileName)
{
    if (fileName.compare(QStringLiteral("language_cpp.dll"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("C++ Language Support");
    if (fileName.compare(QStringLiteral("language_python.dll"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Python Language Support");

    QString name = QFileInfo(fileName).completeBaseName();
    name.remove(QStringLiteral("language_"), Qt::CaseInsensitive);
    const auto words = name.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    QStringList title;
    for (QString word : words) {
        if (!word.isEmpty()) word[0] = word[0].toUpper();
        title.push_back(word);
    }
    return title.join(QLatin1Char(' ')) + QStringLiteral(" Language Support");
}

QStringList extensionsForAsset(const QString &fileName)
{
    if (fileName.compare(QStringLiteral("language_cpp.dll"), Qt::CaseInsensitive) == 0)
        return {QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
                QStringLiteral("cxx"), QStringLiteral("h"), QStringLiteral("hh"),
                QStringLiteral("hpp"), QStringLiteral("hxx"), QStringLiteral("ixx"),
                QStringLiteral("cppm")};
    if (fileName.compare(QStringLiteral("language_python.dll"), Qt::CaseInsensitive) == 0)
        return {QStringLiteral("py"), QStringLiteral("pyw"), QStringLiteral("pyi")};
    return {};
}

QString descriptionForAsset(const QString &fileName)
{
    if (fileName.compare(QStringLiteral("language_cpp.dll"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("C and C++ highlighting, completion, and live syntax diagnostics.");
    if (fileName.compare(QStringLiteral("language_python.dll"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Python highlighting, completion, diagnostics, and native file runner.");
    return QStringLiteral("Native language support plugin from the Native Studio GitHub release.");
}

QString currentApplicationRelease()
{
#ifdef NATIVE_STUDIO_VERSION
    return QStringLiteral("v") + QStringLiteral(NATIVE_STUDIO_VERSION);
#else
    return {};
#endif
}

bool releaseIsNewer(QString available, QString installed)
{
    if (available.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) available.remove(0, 1);
    if (installed.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) installed.remove(0, 1);
    const auto availableVersion = QVersionNumber::fromString(available);
    const auto installedVersion = QVersionNumber::fromString(installed);
    if (!availableVersion.isNull() && !installedVersion.isNull())
        return QVersionNumber::compare(availableVersion, installedVersion) > 0;
    return available != installed;
}

void applyGitHubHeaders(QNetworkRequest &request)
{
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "Native-Studio-Plugin-Manager");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
}
}

PluginManagerDialog::PluginManagerDialog(LanguageManager *languageManager, QWidget *parent)
    : QDialog(parent)
    , m_languageManager(languageManager)
    , m_network(new QNetworkAccessManager(this))
    , m_search(new QLineEdit(this))
    , m_table(new QTableWidget(this))
    , m_status(new QLabel(this))
    , m_installButton(new QPushButton(tr("Install"), this))
    , m_removeButton(new QPushButton(tr("Remove"), this))
    , m_refreshButton(new QPushButton(tr("Refresh"), this))
{
    setObjectName(QStringLiteral("pluginExplorer"));
    setWindowTitle(tr("Plugin Explorer"));
    resize(900, 560);
    setModal(true);

    auto *heading = new QLabel(tr("PLUGIN EXPLORER"), this);
    heading->setObjectName(QStringLiteral("pluginExplorerHeading"));
    auto *description = new QLabel(
        tr("Browse language plugins published on github.com/%1/releases")
            .arg(QString::fromLatin1(PluginRepository)), this);
    description->setObjectName(QStringLiteral("pluginExplorerDescription"));
    m_search->setObjectName(QStringLiteral("pluginSearch"));
    m_search->setPlaceholderText(tr("Search plugins or file extensions"));
    m_refreshButton->setObjectName(QStringLiteral("refreshPluginCatalog"));

    auto *searchLayout = new QHBoxLayout;
    searchLayout->addWidget(m_search, 1);
    searchLayout->addWidget(m_refreshButton);

    m_table->setColumnCount(5);
    m_table->setObjectName(QStringLiteral("pluginTable"));
    m_table->setHorizontalHeaderLabels({tr("Plugin"), tr("File types"), tr("Installed"),
                                        tr("Latest"), tr("Status")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    m_installButton->setObjectName(QStringLiteral("installPluginButton"));
    m_installButton->setProperty("accent", true);
    m_removeButton->setObjectName(QStringLiteral("removePluginButton"));
    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *actions = new QHBoxLayout;
    actions->addWidget(m_installButton);
    actions->addWidget(m_removeButton);
    actions->addStretch();
    actions->addWidget(closeButtons);

    m_status->setWordWrap(true);
    m_status->setObjectName(QStringLiteral("pluginExplorerStatus"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addLayout(searchLayout);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_status);
    layout->addLayout(actions);

    connect(m_refreshButton, &QPushButton::clicked, this, &PluginManagerDialog::refreshCatalog);
    connect(m_installButton, &QPushButton::clicked, this, &PluginManagerDialog::installOrUpdateSelected);
    connect(m_removeButton, &QPushButton::clicked, this, &PluginManagerDialog::removeSelected);
    connect(m_search, &QLineEdit::textChanged, this, &PluginManagerDialog::filterPlugins);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &PluginManagerDialog::updateActions);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refreshTable();
    QTimer::singleShot(0, this, &PluginManagerDialog::refreshCatalog);
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

        const QString displayName = local ? local->name : remote->displayName;
        auto *nameItem = new QTableWidgetItem(displayName);
        nameItem->setToolTip(remote ? remote->description : QString{});
        nameItem->setData(FileNameRole, fileName);
        nameItem->setData(DownloadUrlRole, remote ? remote->downloadUrl : QString{});
        nameItem->setData(BundledRole, local ? local->bundled : false);
        nameItem->setData(FilePathRole, local ? local->filePath : QString{});
        nameItem->setData(RemoteVersionRole, remote ? remote->version : QString{});

        QString state;
        if (!local) {
            state = tr("Available");
        } else if (!remote) {
            state = tr("Installed");
        } else {
            const QString installedRelease = local->bundled
                ? currentApplicationRelease()
                : QSettings().value(QStringLiteral("plugins/installedRelease/") + fileName).toString();
            state = installedRelease.isEmpty() || releaseIsNewer(remote->version, installedRelease)
                ? tr("Update available") : tr("Installed");
        }
        nameItem->setData(StatusRole, state);
        m_table->setItem(row, 0, nameItem);
        const QStringList extensions = local ? local->extensions : remote->extensions;
        QStringList formattedExtensions;
        for (const auto &extension : extensions)
            formattedExtensions.push_back(QLatin1Char('.') + extension);
        m_table->setItem(row, 1, new QTableWidgetItem(formattedExtensions.join(QStringLiteral(", "))));
        m_table->setItem(row, 2, new QTableWidgetItem(local ? local->version : tr("Not installed")));
        m_table->setItem(row, 3, new QTableWidgetItem(remote ? remote->version : QStringLiteral("—")));
        auto *statusItem = new QTableWidgetItem(state);
        statusItem->setForeground(state == tr("Update available")
                                      ? QColor(QStringLiteral("#d29922"))
                                      : state == tr("Available")
                                            ? QColor(QStringLiteral("#58a6ff"))
                                            : QColor(QStringLiteral("#3fb950")));
        m_table->setItem(row, 4, statusItem);
    }
    if (m_table->rowCount() > 0) m_table->selectRow(0);
    filterPlugins(m_search->text());
    updateActions();
}

void PluginManagerDialog::refreshCatalog()
{
    m_refreshButton->setEnabled(false);
    setStatus(tr("Loading plugins from GitHub…"));

    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                                     .arg(QString::fromLatin1(PluginRepository))));
    applyGitHubHeaders(request);
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray payload = reply->readAll();
        m_refreshButton->setEnabled(true);
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
            m_remotePlugins.push_back({name, displayNameForAsset(name), descriptionForAsset(name),
                extensionsForAsset(name), version,
                asset.value(QStringLiteral("browser_download_url")).toString()});
        }
        refreshTable();
        setStatus(m_remotePlugins.isEmpty()
            ? tr("The latest GitHub release has no language plugins.")
            : tr("%1 plugin(s) available from GitHub release %2.")
                  .arg(m_remotePlugins.size()).arg(version));
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
        setStatus(tr("This plugin is not available in the current GitHub release."), true);
        return;
    }

    const QString version = item->data(RemoteVersionRole).toString();
    setStatus(tr("Downloading %1…").arg(fileName));
    QNetworkRequest request{QUrl(url)};
    applyGitHubHeaders(request);
    auto *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fileName, version] {
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
        settings.setValue(QStringLiteral("plugins/installedRelease/") + fileName, version);
        setStatus(tr("%1 is staged. Restart Native Studio to finish installation.").arg(fileName));
        const int row = selectedRow();
        if (row >= 0) {
            m_table->item(row, 0)->setData(StatusRole, tr("Restart required"));
            m_table->item(row, 4)->setText(tr("Restart required"));
            m_table->item(row, 4)->setForeground(QColor(QStringLiteral("#d29922")));
            updateActions();
        }
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
    m_table->item(row, 0)->setData(StatusRole, tr("Removal pending"));
    m_table->item(row, 4)->setText(tr("Removal pending"));
    m_table->item(row, 4)->setForeground(QColor(QStringLiteral("#d29922")));
    updateActions();
}

void PluginManagerDialog::filterPlugins(const QString &query)
{
    const QString needle = query.trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        bool matches = needle.isEmpty();
        for (int column = 0; !matches && column < m_table->columnCount(); ++column) {
            const auto *item = m_table->item(row, column);
            matches = item && item->text().contains(needle, Qt::CaseInsensitive);
        }
        m_table->setRowHidden(row, !matches);
    }
}

void PluginManagerDialog::updateActions()
{
    const int row = selectedRow();
    const auto *item = row >= 0 ? m_table->item(row, 0) : nullptr;
    const QString state = item ? item->data(StatusRole).toString() : QString{};
    const bool downloadable = item && !item->data(DownloadUrlRole).toString().isEmpty();
    const bool installed = item && !item->data(FilePathRole).toString().isEmpty();
    m_installButton->setText(state == tr("Update available") ? tr("Update") : tr("Install"));
    m_installButton->setEnabled(downloadable && state != tr("Installed")
                                && state != tr("Restart required")
                                && state != tr("Removal pending"));
    m_removeButton->setEnabled(installed && state != tr("Removal pending"));
}

void PluginManagerDialog::setStatus(const QString &message, bool error)
{
    m_status->setText(message);
    m_status->setStyleSheet(error ? QStringLiteral("color: #f48771;") : QStringLiteral("color: #cccccc;"));
}
