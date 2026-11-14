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

struct LanguagePluginInfo
{
    QString id;
    QString name;
    QString version;
    QString filePath;
    QStringList extensions;
    bool bundled = false;
};

class LanguageManager final : public QObject
{
    Q_OBJECT

public:
    explicit LanguageManager(QObject *parent = nullptr);
    ~LanguageManager() override;

    int loadPlugins(const QString &directory, bool bundled = false);
    int loadDefaultPlugins();
    [[nodiscard]] QStringList languages() const;
    [[nodiscard]] QList<LanguagePluginInfo> pluginInfos() const;
    [[nodiscard]] QString languageForFile(const QString &filePath) const;
    [[nodiscard]] QSyntaxHighlighter *createHighlighter(const QString &filePath,
                                                        QTextDocument *document) const;
    [[nodiscard]] RunConfiguration runConfiguration(const QString &filePath) const;
    [[nodiscard]] static QString userPluginDirectory();
    static void applyPendingOperations();

private:
    struct LoadedPlugin {
        std::unique_ptr<QPluginLoader> loader;
        LanguagePlugin *plugin = nullptr;
        LanguagePluginInfo info;
    };

    std::vector<LoadedPlugin> m_plugins;
    QHash<QString, LanguagePlugin *> m_byExtension;
};
