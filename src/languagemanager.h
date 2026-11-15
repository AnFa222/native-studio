#pragma once

#include "languageplugin.h"

#include <QObject>
#include <QHash>
#include <QStringList>

#include <memory>
#include <vector>

class QPluginLoader;
class QSyntaxHighlighter;
class QTextDocument;
class QWidget;

struct LanguagePluginInfo
{
    QString id;
    QString name;
    QString version;
    QString filePath;
    QStringList extensions;
    bool bundled = false;
};

struct LanguageStatusWidgetInfo
{
    QWidget *widget = nullptr;
    QStringList extensions;
};

struct LanguageSettingsPageInfo
{
    QString title;
    QStringList keywords;
    QWidget *widget = nullptr;
};

class LanguageManager final : public QObject
{
    Q_OBJECT

public:
    explicit LanguageManager(QObject *parent = nullptr);
    ~LanguageManager() override;

    int loadPlugins(const QString &directory, bool bundled = false);
    int loadDefaultPlugins();
    void loadDefaultPluginsAsync();
    [[nodiscard]] QStringList languages() const;
    [[nodiscard]] QList<LanguagePluginInfo> pluginInfos() const;
    [[nodiscard]] QString languageForFile(const QString &filePath) const;
    [[nodiscard]] QSyntaxHighlighter *createHighlighter(const QString &filePath,
                                                        QTextDocument *document) const;
    [[nodiscard]] RunConfiguration runConfiguration(const QString &filePath) const;
    [[nodiscard]] QStringList completionItems(const QString &filePath,
                                              const QString &documentText) const;
    [[nodiscard]] QList<CodeDiagnostic> diagnostics(const QString &filePath,
                                                    const QString &documentText) const;
    [[nodiscard]] int indentationDeltaAfterLine(const QString &filePath,
                                                const QString &lineText) const;
    [[nodiscard]] QList<CodeSymbol> documentSymbols(const QString &filePath,
                                                    const QString &documentText) const;
    [[nodiscard]] QList<LanguageStatusWidgetInfo> createStatusWidgets(QWidget *parent) const;
    [[nodiscard]] QList<LanguageSettingsPageInfo> createSettingsPages(QWidget *parent) const;
    [[nodiscard]] static QString userPluginDirectory();
    static void applyPendingOperations();

signals:
    void pluginLoaded(const QString &languageName);
    void loadingFinished(int loadedCount);

private:
    struct LoadedPlugin {
        std::unique_ptr<QPluginLoader> loader;
        QObject *instance = nullptr;
        LanguagePlugin *plugin = nullptr;
        LanguagePluginInfo info;
    };

    struct PendingPlugin {
        QString filePath;
        bool bundled = false;
    };

    bool loadPluginFile(const QString &filePath, bool bundled);
    void queuePluginDirectory(const QString &directory, bool bundled);
    void loadNextPlugin();

    std::vector<LoadedPlugin> m_plugins;
    QHash<QString, LanguagePlugin *> m_byExtension;
    QList<PendingPlugin> m_pendingPlugins;
    int m_asyncLoadedCount = 0;
    bool m_asyncLoading = false;
};
