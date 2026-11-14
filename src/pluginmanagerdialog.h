#pragma once

#include <QDialog>
#include <QList>
#include <QStringList>

class LanguageManager;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
class QTableWidget;
class QLabel;

class PluginManagerDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit PluginManagerDialog(LanguageManager *languageManager, QWidget *parent = nullptr);

private slots:
    void refreshCatalog();
    void installOrUpdateSelected();
    void removeSelected();
    void filterPlugins(const QString &query);
    void updateActions();

private:
    struct RemotePlugin {
        QString fileName;
        QString displayName;
        QString description;
        QStringList extensions;
        QString version;
        QString downloadUrl;
    };

    void refreshTable();
    [[nodiscard]] int selectedRow() const;
    void setStatus(const QString &message, bool error = false);

    LanguageManager *m_languageManager = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QLineEdit *m_search = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_installButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QList<RemotePlugin> m_remotePlugins;
};
