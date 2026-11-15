#include "extensionmanagerdialog.h"

#include "extensionmanager.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

ExtensionManagerDialog::ExtensionManagerDialog(ExtensionManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setObjectName(QStringLiteral("extensionManagerDialog"));
    setWindowTitle(tr("Native Extension Manager"));
    resize(880, 520);

    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(tr("Native Extensions"), this);
    heading->setObjectName(QStringLiteral("extensionManagerHeading"));
    layout->addWidget(heading);
    auto *description = new QLabel(
        tr("Import JSON manifests, validate native ABI compatibility, and manage installed binaries."),
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
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    refreshButton->setObjectName(QStringLiteral("refreshExtensions"));
    m_toggle = new QPushButton(tr("Disable"), this);
    m_toggle->setObjectName(QStringLiteral("toggleExtension"));
    m_remove = new QPushButton(tr("Remove"), this);
    m_remove->setObjectName(QStringLiteral("removeExtension"));
    actions->addWidget(import);
    actions->addWidget(refreshButton);
    actions->addStretch();
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
    connect(m_toggle, &QPushButton::clicked, this, &ExtensionManagerDialog::toggleSelected);
    connect(m_remove, &QPushButton::clicked, this, &ExtensionManagerDialog::removeSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refresh();
}

QString ExtensionManagerDialog::selectedId() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0)) return {};
    return m_table->item(row, 0)->data(Qt::UserRole).toString();
}

void ExtensionManagerDialog::refresh()
{
    const QString previous = selectedId();
    const auto extensions = m_manager ? m_manager->installedExtensions()
                                      : QList<NativeExtensionInfo>{};
    m_table->setRowCount(extensions.size());
    for (int row = 0; row < extensions.size(); ++row) {
        const auto &extension = extensions.at(row);
        QString status;
        if (extension.pendingRestart) status = tr("Restart required");
        else if (!extension.enabled) status = tr("Disabled");
        else if (extension.loaded) status = tr("Active");
        else status = tr("Installed");
        const QStringList values{
            extension.name, extension.id, extension.version,
            QString::number(extension.abiVersion),
            extension.bundled ? tr("Bundled") : tr("User"), status
        };
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            item->setData(Qt::UserRole, extension.id);
            if (column == 0)
                item->setToolTip(extension.description.isEmpty() ? extension.manifestPath
                                                                  : extension.description);
            m_table->setItem(row, column, item);
        }
        if (extension.id == previous) m_table->selectRow(row);
    }
    if (m_table->currentRow() < 0 && m_table->rowCount() > 0) m_table->selectRow(0);
    m_status->setText(tr("%1 extension(s) installed · Native ABI %2")
                          .arg(extensions.size()).arg(NS_EXTENSION_ABI_VERSION));
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
    const auto extensions = m_manager ? m_manager->installedExtensions()
                                      : QList<NativeExtensionInfo>{};
    const auto found = std::find_if(extensions.cbegin(), extensions.cend(), [&id](const auto &item) {
        return item.id == id;
    });
    const bool selected = found != extensions.cend();
    m_toggle->setEnabled(selected);
    m_remove->setEnabled(selected && !found->bundled);
    if (!selected) {
        m_details->clear();
        return;
    }
    m_toggle->setText(found->enabled ? tr("Disable") : tr("Enable"));
    const QString policy = found->bundled
        ? tr("\nBundled extension: it can be disabled, but not removed.") : QString{};
    m_details->setText(tr("%1%2%3\nManifest: %4")
        .arg(found->description,
             found->author.isEmpty() ? QString{} : tr(" — %1").arg(found->author),
             policy,
             QDir::toNativeSeparators(found->manifestPath)));
}

void ExtensionManagerDialog::importManifest()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Native Extension Manifest"), {}, tr("Extension manifests (*.json)"));
    if (path.isEmpty()) return;
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
