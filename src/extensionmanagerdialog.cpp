#include "extensionmanagerdialog.h"

#include "extensionmanager.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int CatalogIndexRole = Qt::UserRole + 1;
constexpr auto ExtensionCatalogUrl =
    "https://raw.githubusercontent.com/AnFa222/native-studio/main/extension-store/index.json";

bool newerVersion(const QString &available, const QString &installed)
{
    const auto left = QVersionNumber::fromString(available);
    const auto right = QVersionNumber::fromString(installed);
    if (!left.isNull() && !right.isNull()) return QVersionNumber::compare(left, right) > 0;
    return available != installed;
}

void applyGitHubHeaders(QNetworkRequest &request)
{
    request.setRawHeader("User-Agent", "Native-Studio-Extension-Store");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
}
}

ExtensionManagerDialog::ExtensionManagerDialog(ExtensionManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager), m_network(new QNetworkAccessManager(this))
{
    setObjectName(QStringLiteral("extensionManagerDialog"));
    setWindowTitle(tr("Native Extension Manager"));
    resize(880, 520);

    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("Native Extensions"), this);
    heading->setObjectName(QStringLiteral("extensionManagerHeading"));
    layout->addWidget(heading);
    auto *description = new QLabel(
        tr("Browse the GitHub extension store or import a native extension manifest."),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("extensionSearch"));
    m_search->setPlaceholderText(tr("Search extensions…"));
    layout->addWidget(m_search);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("extensionTable"));
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {tr("Name"), tr("ID"), tr("Version"), tr("ABI"), tr("Source"), tr("Status")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    m_details = new QLabel(this);
    m_details->setObjectName(QStringLiteral("extensionDetails"));
    m_details->setWordWrap(true);
    layout->addWidget(m_details);

    auto *actions = new QHBoxLayout;
    auto *import = new QPushButton(tr("Import / Update Manifest…"), this);
    import->setObjectName(QStringLiteral("importExtensionManifest"));
    auto *refreshButton = new QPushButton(tr("Refresh Installed"), this);
    refreshButton->setObjectName(QStringLiteral("refreshExtensions"));
    m_refreshCatalog = new QPushButton(tr("Refresh Store"), this);
    m_refreshCatalog->setObjectName(QStringLiteral("refreshExtensionCatalog"));
    m_install = new QPushButton(tr("Install"), this);
    m_install->setObjectName(QStringLiteral("installExtensionButton"));
    m_install->setProperty("accent", true);
    m_toggle = new QPushButton(tr("Disable"), this);
    m_toggle->setObjectName(QStringLiteral("toggleExtension"));
    m_remove = new QPushButton(tr("Remove"), this);
    m_remove->setObjectName(QStringLiteral("removeExtension"));
    actions->addWidget(import);
    actions->addWidget(refreshButton);
    actions->addWidget(m_refreshCatalog);
    actions->addStretch();
    actions->addWidget(m_install);
    actions->addWidget(m_toggle);
    actions->addWidget(m_remove);
    layout->addLayout(actions);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("extensionManagerStatus"));
    layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &ExtensionManagerDialog::filterRows);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &ExtensionManagerDialog::updateSelection);
    connect(import, &QPushButton::clicked, this, &ExtensionManagerDialog::importManifest);
    connect(refreshButton, &QPushButton::clicked, this, &ExtensionManagerDialog::refresh);
    connect(m_refreshCatalog, &QPushButton::clicked,
            this, &ExtensionManagerDialog::refreshCatalog);
    connect(m_install, &QPushButton::clicked,
            this, &ExtensionManagerDialog::installOrUpdateSelected);
    connect(m_toggle, &QPushButton::clicked, this, &ExtensionManagerDialog::toggleSelected);
    connect(m_remove, &QPushButton::clicked, this, &ExtensionManagerDialog::removeSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refresh();
    QTimer::singleShot(0, this, &ExtensionManagerDialog::refreshCatalog);
}

QString ExtensionManagerDialog::selectedId() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0)) return {};
    return m_table->item(row, 0)->data(Qt::UserRole).toString();
}

int ExtensionManagerDialog::selectedCatalogIndex() const
{
    const int row = m_table->currentRow();
    return row >= 0 && m_table->item(row, 0)
        ? m_table->item(row, 0)->data(CatalogIndexRole).toInt() : -1;
}

void ExtensionManagerDialog::refresh()
{
    const QString previous = selectedId();
    const auto extensions = m_manager ? m_manager->installedExtensions()
                                      : QList<NativeExtensionInfo>{};
    QStringList ids;
    for (const auto &extension : extensions) ids.push_back(extension.id);
    int catalogOnly = 0;
    for (const auto &entry : m_catalog)
        if (!ids.contains(entry.id, Qt::CaseInsensitive)) ++catalogOnly;
    m_table->setRowCount(extensions.size() + catalogOnly);
    int row = 0;
    for (const auto &extension : extensions) {
        QString status;
        if (extension.pendingRestart) status = tr("Restart required");
        else if (!extension.enabled) status = tr("Disabled");
        else if (extension.loaded) status = tr("Active");
        else status = tr("Installed");
        int catalogIndex = -1;
        for (int index = 0; index < m_catalog.size(); ++index) {
            if (m_catalog.at(index).id.compare(extension.id, Qt::CaseInsensitive) == 0) {
                catalogIndex = index;
                if (!extension.bundled
                    && newerVersion(m_catalog.at(index).version, extension.version))
                    status = tr("Update available");
                break;
            }
        }
        const QStringList values{
            extension.name, extension.id, extension.version,
            QString::number(extension.abiVersion),
            extension.bundled ? tr("Bundled") : tr("User"), status
        };
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setData(Qt::UserRole, extension.id);
            item->setData(CatalogIndexRole, catalogIndex);
            if (column == 0)
                item->setToolTip(extension.description.isEmpty() ? extension.manifestPath
                                                                  : extension.description);
            m_table->setItem(row, column, item);
        }
        if (extension.id == previous) m_table->selectRow(row);
        ++row;
    }
    for (int index = 0; index < m_catalog.size(); ++index) {
        const auto &entry = m_catalog.at(index);
        if (ids.contains(entry.id, Qt::CaseInsensitive)) continue;
        const QStringList values{entry.name, entry.id, entry.version,
            QString::number(NS_EXTENSION_ABI_VERSION), tr("GitHub Store"), tr("Available")};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setData(Qt::UserRole, entry.id);
            item->setData(CatalogIndexRole, index);
            if (column == 0) item->setToolTip(entry.description);
            m_table->setItem(row, column, item);
        }
        if (entry.id == previous) m_table->selectRow(row);
        ++row;
    }
    if (m_table->currentRow() < 0 && m_table->rowCount() > 0) m_table->selectRow(0);
    m_status->setText(tr("%1 installed · %2 in GitHub store · Native ABI %3")
                          .arg(extensions.size()).arg(m_catalog.size())
                          .arg(NS_EXTENSION_ABI_VERSION));
    filterRows(m_search->text());
    updateSelection();
}

void ExtensionManagerDialog::filterRows(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        bool matches = needle.isEmpty();
        for (int column = 0; !matches && column < m_table->columnCount(); ++column) {
            const auto *item = m_table->item(row, column);
            matches = item && item->text().contains(needle, Qt::CaseInsensitive);
        }
        m_table->setRowHidden(row, !matches);
    }
}

void ExtensionManagerDialog::updateSelection()
{
    const QString id = selectedId();
    const int catalogIndex = selectedCatalogIndex();
    const auto extensions = m_manager ? m_manager->installedExtensions()
                                      : QList<NativeExtensionInfo>{};
    const auto found = std::find_if(extensions.cbegin(), extensions.cend(), [&id](const auto &item) {
        return item.id == id;
    });
    const bool selected = found != extensions.cend();
    const bool inCatalog = catalogIndex >= 0 && catalogIndex < m_catalog.size();
    m_toggle->setEnabled(selected);
    m_remove->setEnabled(selected && !found->bundled);
    const bool updateAvailable = selected && inCatalog && !found->bundled
        && newerVersion(m_catalog.at(catalogIndex).version, found->version);
    m_install->setEnabled(inCatalog && (!selected || updateAvailable));
    m_install->setText(updateAvailable ? tr("Update") : tr("Install"));
    if (!selected) {
        if (inCatalog) {
            const auto &entry = m_catalog.at(catalogIndex);
            m_details->setText(tr("%1%2\nVersion: %3\nPermissions: %4")
                .arg(entry.description,
                     entry.author.isEmpty() ? QString{} : tr(" — %1").arg(entry.author),
                     entry.version,
                     entry.permissions.isEmpty() ? tr("None")
                                                  : entry.permissions.join(QStringLiteral(", "))));
        } else {
            m_details->clear();
        }
        return;
    }
    m_toggle->setText(found->enabled ? tr("Disable") : tr("Enable"));
    const QString policy = found->bundled
        ? tr("\nBundled extension: it can be disabled, but not removed.") : QString{};
    const QString permissions = found->permissions.isEmpty()
        ? tr("None") : found->permissions.join(QStringLiteral(", "));
    m_details->setText(tr("%1%2%3\nPermissions: %4\nManifest: %5")
        .arg(found->description,
             found->author.isEmpty() ? QString{} : tr(" — %1").arg(found->author),
             policy,
             permissions,
             QDir::toNativeSeparators(found->manifestPath)));
}

void ExtensionManagerDialog::setCatalogStatus(const QString &message, bool error)
{
    m_status->setText(message);
    m_status->setStyleSheet(error ? QStringLiteral("color: #f85149;") : QString{});
}

void ExtensionManagerDialog::refreshCatalog()
{
    m_refreshCatalog->setEnabled(false);
    setCatalogStatus(tr("Loading the GitHub extension store…"));
    QNetworkRequest request(QUrl(QString::fromLatin1(ExtensionCatalogUrl)));
    applyGitHubHeaders(request);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_refreshCatalog->setEnabled(true);
        if (reply->error() != QNetworkReply::NoError) {
            setCatalogStatus(tr("Could not load the extension store: %1")
                                 .arg(reply->errorString()), true);
            reply->deleteLater();
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        reply->deleteLater();
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            setCatalogStatus(tr("The extension store returned invalid JSON."), true);
            return;
        }
        QList<CatalogEntry> catalog;
        for (const QJsonValue &value : document.object()
                 .value(QStringLiteral("extensions")).toArray()) {
            const QJsonObject object = value.toObject();
            CatalogEntry entry;
            entry.id = object.value(QStringLiteral("id")).toString().trimmed();
            entry.name = object.value(QStringLiteral("name")).toString().trimmed();
            entry.version = object.value(QStringLiteral("version")).toString().trimmed();
            entry.description = object.value(QStringLiteral("description")).toString();
            entry.author = object.value(QStringLiteral("author")).toString();
            entry.manifestUrl = object.value(QStringLiteral("manifestUrl")).toString();
#ifdef Q_OS_WIN
            entry.binaryUrl = object.value(QStringLiteral("binaries")).toObject()
                                  .value(QStringLiteral("windows")).toString();
#elif defined(Q_OS_MACOS)
            entry.binaryUrl = object.value(QStringLiteral("binaries")).toObject()
                                  .value(QStringLiteral("macos")).toString();
#else
            entry.binaryUrl = object.value(QStringLiteral("binaries")).toObject()
                                  .value(QStringLiteral("linux")).toString();
#endif
            for (const QJsonValue &permission : object.value(QStringLiteral("permissions")).toArray())
                entry.permissions.push_back(permission.toString());
            if (!entry.id.isEmpty() && !entry.name.isEmpty() && !entry.version.isEmpty()
                && !entry.manifestUrl.isEmpty() && !entry.binaryUrl.isEmpty()
                && QUrl(entry.manifestUrl).isValid() && QUrl(entry.binaryUrl).isValid())
                catalog.push_back(std::move(entry));
        }
        m_catalog = std::move(catalog);
        refresh();
    });
}

void ExtensionManagerDialog::installOrUpdateSelected()
{
    const int index = selectedCatalogIndex();
    if (index < 0 || index >= m_catalog.size()) return;
    const CatalogEntry entry = m_catalog.at(index);
    const QString permissionText = entry.permissions.isEmpty()
        ? tr("This extension declares no host permissions.")
        : tr("This extension requests:\n\n• %1")
              .arg(entry.permissions.join(QStringLiteral("\n• ")));
    if (QMessageBox::question(this, tr("Install Native Extension"),
            tr("Install %1 %2 from GitHub?\n\n%3\n\nNative extensions run in-process. "
               "Install only extensions you trust.")
                .arg(entry.name, entry.version, permissionText),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    m_install->setEnabled(false);
    setCatalogStatus(tr("Downloading %1 manifest…").arg(entry.name));
    QNetworkRequest request(QUrl(entry.manifestUrl));
    applyGitHubHeaders(request);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, entry] {
        if (reply->error() != QNetworkReply::NoError) {
            setCatalogStatus(tr("Manifest download failed: %1").arg(reply->errorString()), true);
            reply->deleteLater();
            updateSelection();
            return;
        }
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        downloadBinary(entry, data);
    });
}

void ExtensionManagerDialog::downloadBinary(const CatalogEntry &entry,
                                             const QByteArray &manifestData)
{
    const QJsonDocument manifest = QJsonDocument::fromJson(manifestData);
    if (!manifest.isObject() || manifest.object().value(QStringLiteral("id")).toString()
            .compare(entry.id, Qt::CaseInsensitive) != 0) {
        setCatalogStatus(tr("The downloaded manifest does not match the catalog entry."), true);
        updateSelection();
        return;
    }
    m_downloadDirectory = std::make_unique<QTemporaryDir>();
    if (!m_downloadDirectory->isValid()) {
        setCatalogStatus(tr("Could not create a temporary download directory."), true);
        updateSelection();
        return;
    }
    const QJsonValue binaryValue = manifest.object().value(QStringLiteral("binary"));
#ifdef Q_OS_WIN
    const QString binaryName = binaryValue.isObject()
        ? binaryValue.toObject().value(QStringLiteral("windows")).toString()
        : binaryValue.toString();
#elif defined(Q_OS_MACOS)
    const QString binaryName = binaryValue.isObject()
        ? binaryValue.toObject().value(QStringLiteral("macos")).toString()
        : binaryValue.toString();
#else
    const QString binaryName = binaryValue.isObject()
        ? binaryValue.toObject().value(QStringLiteral("linux")).toString()
        : binaryValue.toString();
#endif
    if (binaryName.isEmpty() || QFileInfo(binaryName).fileName() != binaryName) {
        setCatalogStatus(tr("The extension manifest contains an invalid binary name."), true);
        updateSelection();
        return;
    }
    const QString manifestPath = m_downloadDirectory->filePath(QStringLiteral("extension.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::WriteOnly) || manifestFile.write(manifestData) < 0) {
        setCatalogStatus(tr("Could not stage the downloaded manifest."), true);
        updateSelection();
        return;
    }
    manifestFile.close();
    setCatalogStatus(tr("Downloading %1…").arg(entry.name));
    QNetworkRequest request(QUrl(entry.binaryUrl));
    applyGitHubHeaders(request);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, entry, manifestPath, binaryName] {
        if (reply->error() != QNetworkReply::NoError) {
            setCatalogStatus(tr("Binary download failed: %1").arg(reply->errorString()), true);
            reply->deleteLater();
            updateSelection();
            return;
        }
        QFile binary(m_downloadDirectory->filePath(binaryName));
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        if (!binary.open(QIODevice::WriteOnly) || binary.write(data) != data.size()) {
            setCatalogStatus(tr("Could not stage the downloaded extension binary."), true);
            updateSelection();
            return;
        }
        binary.close();
        QString error;
        bool restart = false;
        if (!m_manager->importExtension(manifestPath, &error, &restart)) {
            setCatalogStatus(tr("Installation failed: %1").arg(error), true);
            updateSelection();
            return;
        }
        m_downloadDirectory.reset();
        refresh();
        setCatalogStatus(restart
            ? tr("%1 will be updated after restarting Native Studio.").arg(entry.name)
            : tr("%1 was installed and activated.").arg(entry.name));
    });
}

void ExtensionManagerDialog::importManifest()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Native Extension Manifest"), {}, tr("Extension manifests (*.json)"));
    if (path.isEmpty()) return;
    QFile manifest(path);
    if (manifest.open(QIODevice::ReadOnly)) {
        const QJsonObject object = QJsonDocument::fromJson(manifest.readAll()).object();
        QStringList permissions;
        for (const QJsonValue &value : object.value(QStringLiteral("permissions")).toArray())
            permissions.push_back(value.toString());
        const QString permissionText = permissions.isEmpty()
            ? tr("This extension declares no host permissions.")
            : tr("This extension requests:\n\n• %1").arg(
                  permissions.join(QStringLiteral("\n• ")));
        if (QMessageBox::question(this, tr("Install Native Extension"),
                tr("%1\n\nNative extensions run in-process. Install only extensions you trust.")
                    .arg(permissionText), QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) != QMessageBox::Yes) return;
    }
    QString error;
    bool restart = false;
    if (!m_manager->importExtension(path, &error, &restart)) {
        QMessageBox::critical(this, tr("Extension Import Failed"), error);
        return;
    }
    refresh();
    QMessageBox::information(this, tr("Extension Imported"),
        restart ? tr("The extension update will be applied after restarting Native Studio.")
                : tr("The extension was installed and activated."));
}

void ExtensionManagerDialog::toggleSelected()
{
    const QString id = selectedId();
    const auto extensions = m_manager->installedExtensions();
    const auto found = std::find_if(extensions.cbegin(), extensions.cend(), [&id](const auto &item) {
        return item.id == id;
    });
    if (found == extensions.cend()) return;
    QString error;
    if (!m_manager->setExtensionEnabled(id, !found->enabled, &error)) {
        QMessageBox::critical(this, tr("Could Not Update Extension"), error);
        return;
    }
    refresh();
    QMessageBox::information(this, tr("Restart Required"),
                             tr("The change will take effect after restarting Native Studio."));
}

void ExtensionManagerDialog::removeSelected()
{
    const QString id = selectedId();
    if (id.isEmpty()) return;
    if (QMessageBox::question(this, tr("Remove Extension"),
                              tr("Remove %1?").arg(id)) != QMessageBox::Yes) return;
    QString error;
    if (!m_manager->removeExtension(id, &error)) {
        QMessageBox::critical(this, tr("Could Not Remove Extension"), error);
        return;
    }
    refresh();
    QMessageBox::information(
        this, tr("Extension Removal"),
        tr("The extension was removed, or scheduled for removal after restarting Native Studio."));
}
