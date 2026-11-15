#pragma once

#include <QDialog>
#include <QList>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

class ExtensionManager;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QNetworkAccessManager;

class ExtensionManagerDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ExtensionManagerDialog(ExtensionManager *manager, QWidget *parent = nullptr);

private slots:
    void refresh();
    void filterRows(const QString &text);
    void updateSelection();
    void importManifest();
    void refreshCatalog();
    void installOrUpdateSelected();
    void toggleSelected();
    void removeSelected();

private:
    [[nodiscard]] QString selectedId() const;
    [[nodiscard]] int selectedCatalogIndex() const;

    struct CatalogEntry {
        QString id;
        QString name;
        QString version;
        QString description;
        QString author;
        QString manifestUrl;
        QString binaryUrl;
        QStringList permissions;
    };
    void downloadBinary(const CatalogEntry &entry, const QByteArray &manifestData);
    void setCatalogStatus(const QString &message, bool error = false);

    ExtensionManager *m_manager = nullptr;
    QLineEdit *m_search = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_details = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_toggle = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_install = nullptr;
    QPushButton *m_refreshCatalog = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QList<CatalogEntry> m_catalog;
    std::unique_ptr<QTemporaryDir> m_downloadDirectory;
};
