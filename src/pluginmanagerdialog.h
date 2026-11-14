#pragma once

#include <QDialog>
#include <QList>

class LanguageManager;
class QLineEdit;
class QNetworkAccessManager;
class QTableWidget;
class QLabel;

class PluginManagerDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit PluginManagerDialog(LanguageManager *languageManager, QWidget *parent = nullptr);

private slots:
    void checkGitHub();
    void installOrUpdateSelected();
    void removeSelected();

private:
    struct RemotePlugin {
        QString fileName;
        QString version;
        QString downloadUrl;
    };

    void refreshTable();
    [[nodiscard]] int selectedRow() const;
    void setStatus(const QString &message, bool error = false);

    LanguageManager *m_languageManager = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QLineEdit *m_repository = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QList<RemotePlugin> m_remotePlugins;
};
