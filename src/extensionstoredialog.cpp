#include "extensionstoredialog.h"

#include "extensionmanager.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int CatalogIndexRole = Qt::UserRole + 1;
constexpr auto CatalogUrl =
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

ExtensionStoreDialog::ExtensionStoreDialog(ExtensionManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager), m_network(new QNetworkAccessManager(this))
{
    setObjectName(QStringLiteral("extensionStoreDialog"));
    setWindowTitle(tr("Extension Store"));
    resize(900, 560);

    auto *heading = new QLabel(tr("EXTENSION STORE"), this);
    heading->setObjectName(QStringLiteral("extensionStoreHeading"));
    auto *description = new QLabel(
        tr("Browse native extensions published in the Native Studio GitHub catalog."), this);
    description->setObjectName(QStringLiteral("extensionStoreDescription"));
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("extensionStoreSearch"));
    m_search->setPlaceholderText(tr("Search extensions…"));
    m_refresh = new QPushButton(tr("Refresh"), this);
    m_refresh->setObjectName(QStringLiteral("refreshExtensionCatalog"));

    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_refresh);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("extensionStoreTable"));
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        {tr("Extension"), tr("Version"), tr("Author"), tr("Installed"), tr("Status")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    m_details = new QLabel(this);
    m_details->setObjectName(QStringLiteral("extensionStoreDetails"));
    m_details->setWordWrap(true);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("extensionStoreStatus"));
    m_status->setWordWrap(true);
    m_install = new QPushButton(tr("Install"), this);
    m_install->setObjectName(QStringLiteral("installExtensionButton"));
    m_install->setProperty("accent", true);
    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *actions = new QHBoxLayout;
    actions->addWidget(m_install);
    actions->addStretch();
    actions->addWidget(closeButtons);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addLayout(searchRow);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_details);
    layout->addWidget(m_status);
    layout->addLayout(actions);

    connect(m_refresh, &QPushButton::clicked, this, &ExtensionStoreDialog::refreshCatalog);
    connect(m_install, &QPushButton::clicked,
            this, &ExtensionStoreDialog::installOrUpdateSelected);
    connect(m_search, &QLineEdit::textChanged, this, &ExtensionStoreDialog::filterRows);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &ExtensionStoreDialog::updateSelection);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rebuildTable();
    QTimer::singleShot(0, this, &ExtensionStoreDialog::refreshCatalog);
}

int ExtensionStoreDialog::selectedCatalogIndex() const
{
    const int row = m_table->currentRow();
    return row >= 0 && m_table->item(row, 0)
        ? m_table->item(row, 0)->data(CatalogIndexRole).toInt() : -1;
}

void ExtensionStoreDialog::rebuildTable()
{
    const int previous = selectedCatalogIndex();
    const auto installed = m_manager ? m_manager->installedExtensions()
                                     : QList<NativeExtensionInfo>{};
    m_table->setRowCount(m_catalog.size());
    for (int row = 0; row < m_catalog.size(); ++row) {
        const auto &entry = m_catalog.at(row);
        const auto found = std::find_if(installed.cbegin(), installed.cend(),
            [&entry](const auto &item) {
                return item.id.compare(entry.id, Qt::CaseInsensitive) == 0;
            });
        const bool isInstalled = found != installed.cend();
        const bool canUpdate = isInstalled && !found->bundled
            && newerVersion(entry.version, found->version);
        QString status = tr("Available");
        if (isInstalled) status = canUpdate ? tr("Update available") : tr("Installed");
        const QStringList values{entry.name, entry.version, entry.author,
            isInstalled ? found->version : QStringLiteral("—"), status};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setData(CatalogIndexRole, row);
            if (column == 0) item->setToolTip(entry.description);
            m_table->setItem(row, column, item);
        }
        if (row == previous) m_table->selectRow(row);
    }
    if (m_table->currentRow() < 0 && m_table->rowCount() > 0) m_table->selectRow(0);
    filterRows(m_search->text());
    updateSelection();
}

void ExtensionStoreDialog::filterRows(const QString &text)
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

void ExtensionStoreDialog::updateSelection()
{
    const int index = selectedCatalogIndex();
    if (index < 0 || index >= m_catalog.size()) {
        m_details->clear();
        m_install->setEnabled(false);
        return;
    }
    const auto &entry = m_catalog.at(index);
    const auto installed = m_manager->installedExtensions();
    const auto found = std::find_if(installed.cbegin(), installed.cend(),
        [&entry](const auto &item) {
            return item.id.compare(entry.id, Qt::CaseInsensitive) == 0;
        });
    const bool isInstalled = found != installed.cend();
    const bool canUpdate = isInstalled && !found->bundled
        && newerVersion(entry.version, found->version);
    m_install->setText(canUpdate ? tr("Update") : tr("Install"));
    m_install->setEnabled(!isInstalled || canUpdate);
    m_details->setText(tr("%1%2\nPermissions: %3")
        .arg(entry.description,
             entry.author.isEmpty() ? QString{} : tr(" — %1").arg(entry.author),
             entry.permissions.isEmpty() ? tr("None")
                                          : entry.permissions.join(QStringLiteral(", "))));
}

void ExtensionStoreDialog::setStatus(const QString &message, bool error)
{
    m_status->setText(message);
    m_status->setStyleSheet(error ? QStringLiteral("color: #f85149;") : QString{});
}

void ExtensionStoreDialog::refreshCatalog()
{
    m_refresh->setEnabled(false);
    setStatus(tr("Loading the GitHub extension store…"));
    QNetworkRequest request(QUrl(QString::fromLatin1(CatalogUrl)));
    applyGitHubHeaders(request);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_refresh->setEnabled(true);
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(tr("Could not load the extension store: %1")
                          .arg(reply->errorString()), true);
            reply->deleteLater();
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        reply->deleteLater();
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            setStatus(tr("The extension store returned invalid JSON."), true);
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
        rebuildTable();
        setStatus(tr("%1 extension(s) available from GitHub.").arg(m_catalog.size()));
    });
}

void ExtensionStoreDialog::installOrUpdateSelected()
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
    setStatus(tr("Downloading %1 manifest…").arg(entry.name));
    QNetworkRequest request(QUrl(entry.manifestUrl));
    applyGitHubHeaders(request);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, entry] {
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(tr("Manifest download failed: %1").arg(reply->errorString()), true);
            reply->deleteLater();
            updateSelection();
            return;
        }
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        downloadBinary(entry, data);
    });
}

void ExtensionStoreDialog::downloadBinary(const CatalogEntry &entry,
                                           const QByteArray &manifestData)
{
    const QJsonDocument manifest = QJsonDocument::fromJson(manifestData);
    if (!manifest.isObject() || manifest.object().value(QStringLiteral("id")).toString()
            .compare(entry.id, Qt::CaseInsensitive) != 0) {
        setStatus(tr("The downloaded manifest does not match the catalog entry."), true);
        updateSelection();
        return;
    }
    m_downloadDirectory = std::make_unique<QTemporaryDir>();
    if (!m_downloadDirectory->isValid()) {
        setStatus(tr("Could not create a temporary download directory."), true);
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
        setStatus(tr("The extension manifest contains an invalid binary name."), true);
        updateSelection();
        return;
    }
    const QString manifestPath = m_downloadDirectory->filePath(QStringLiteral("extension.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::WriteOnly) || manifestFile.write(manifestData) < 0) {
        setStatus(tr("Could not stage the downloaded manifest."), true);
        updateSelection();
        return;
    }
    manifestFile.close();
    setStatus(tr("Downloading %1…").arg(entry.name));
    QNetworkRequest request(QUrl(entry.binaryUrl));
    applyGitHubHeaders(request);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, entry, manifestPath, binaryName] {
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(tr("Binary download failed: %1").arg(reply->errorString()), true);
            reply->deleteLater();
            updateSelection();
            return;
        }
        QFile binary(m_downloadDirectory->filePath(binaryName));
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        if (!binary.open(QIODevice::WriteOnly) || binary.write(data) != data.size()) {
            setStatus(tr("Could not stage the downloaded extension binary."), true);
            updateSelection();
            return;
        }
        binary.close();
        QString error;
        bool restart = false;
        if (!m_manager->importExtension(manifestPath, &error, &restart)) {
            setStatus(tr("Installation failed: %1").arg(error), true);
            updateSelection();
            return;
        }
        m_downloadDirectory.reset();
        rebuildTable();
        setStatus(restart
            ? tr("%1 will be updated after restarting Native Studio.").arg(entry.name)
            : tr("%1 was installed and activated.").arg(entry.name));
    });
}
