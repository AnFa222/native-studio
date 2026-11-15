#pragma once

#include <QDialog>
#include <QList>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

class ExtensionManager;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
class QTableWidget;

class ExtensionStoreDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ExtensionStoreDialog(ExtensionManager *manager, QWidget *parent = nullptr);

private slots:
    void refreshCatalog();
    void filterRows(const QString &text);
    void updateSelection();
    void installOrUpdateSelected();

private:
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

    [[nodiscard]] int selectedCatalogIndex() const;
    void rebuildTable();
    void downloadBinary(const CatalogEntry &entry, const QByteArray &manifestData);
    void requestBinary(const CatalogEntry &entry, const QString &manifestPath,
                       const QString &binaryName, int retriesRemaining);
    void setStatus(const QString &message, bool error = false);

    ExtensionManager *m_manager = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QLineEdit *m_search = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_details = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_install = nullptr;
    QPushButton *m_refresh = nullptr;
    QList<CatalogEntry> m_catalog;
    std::unique_ptr<QTemporaryDir> m_downloadDirectory;
};
