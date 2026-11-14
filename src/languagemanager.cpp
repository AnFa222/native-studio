#include "languagemanager.h"

#include "languageplugin.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonObject>
#include <QPluginLoader>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTextDocument>

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
{
}

LanguageManager::~LanguageManager() = default;

int LanguageManager::loadPlugins(const QString &directory, bool bundled)
{
    QDir pluginDirectory(directory);
    if (!pluginDirectory.exists()) return 0;

    int loadedCount = 0;
    const auto disabled = QSettings().value(QStringLiteral("plugins/disabled")).toStringList();
    const auto files = pluginDirectory.entryInfoList(QDir::Files | QDir::Readable);
    for (const auto &file : files) {
        if (disabled.contains(file.fileName(), Qt::CaseInsensitive)) continue;
        auto loader = std::make_unique<QPluginLoader>(file.absoluteFilePath());
        QObject *instance = loader->instance();
        auto *plugin = qobject_cast<LanguagePlugin *>(instance);
        if (!plugin) {
            loader->unload();
            continue;
        }

        bool alreadyLoaded = false;
        for (const auto &existing : m_plugins) {
            if (existing.plugin->languageName() == plugin->languageName()) {
                alreadyLoaded = true;
                break;
            }
        }
        if (alreadyLoaded) {
            loader->unload();
            continue;
        }

        for (const auto &extension : plugin->fileExtensions()) {
            QString normalized = extension.trimmed().toLower();
            if (normalized.startsWith(QLatin1Char('.'))) normalized.remove(0, 1);
            if (!normalized.isEmpty()) m_byExtension.insert(normalized, plugin);
        }
        const auto metadata = loader->metaData().value(QStringLiteral("MetaData")).toObject();
        LanguagePluginInfo info;
        info.id = file.completeBaseName();
        info.name = metadata.value(QStringLiteral("name")).toString(plugin->languageName());
        info.version = metadata.value(QStringLiteral("version")).toString(QStringLiteral("unknown"));
        info.filePath = file.absoluteFilePath();
        info.extensions = plugin->fileExtensions();
        info.bundled = bundled;
        m_plugins.push_back({std::move(loader), plugin, info});
        ++loadedCount;
    }
    return loadedCount;
}

int LanguageManager::loadDefaultPlugins()
{
    applyPendingOperations();
    int loaded = loadPlugins(userPluginDirectory(), false);
    loaded += loadPlugins(QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("plugins/languages")), true);
    return loaded;
}

QStringList LanguageManager::languages() const
{
    QStringList result;
    result.reserve(static_cast<qsizetype>(m_plugins.size()));
    for (const auto &loaded : m_plugins) result.push_back(loaded.plugin->languageName());
    result.sort(Qt::CaseInsensitive);
    return result;
}

QList<LanguagePluginInfo> LanguageManager::pluginInfos() const
{
    QList<LanguagePluginInfo> result;
    result.reserve(static_cast<qsizetype>(m_plugins.size()));
    for (const auto &loaded : m_plugins) result.push_back(loaded.info);
    return result;
}

QString LanguageManager::languageForFile(const QString &filePath) const
{
    const auto *plugin = m_byExtension.value(QFileInfo(filePath).suffix().toLower(), nullptr);
    return plugin ? plugin->languageName() : QStringLiteral("Plain Text");
}

QSyntaxHighlighter *LanguageManager::createHighlighter(const QString &filePath,
                                                       QTextDocument *document) const
{
    auto *plugin = m_byExtension.value(QFileInfo(filePath).suffix().toLower(), nullptr);
    return plugin ? plugin->createHighlighter(document) : nullptr;
}

RunConfiguration LanguageManager::runConfiguration(const QString &filePath) const
{
    auto *plugin = m_byExtension.value(QFileInfo(filePath).suffix().toLower(), nullptr);
    return plugin ? plugin->runConfiguration(filePath) : RunConfiguration{};
}

QString LanguageManager::userPluginDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("plugins/languages"));
}

void LanguageManager::applyPendingOperations()
{
    QDir directory(userPluginDirectory());
    directory.mkpath(QStringLiteral("."));

    const auto removalMarkers = directory.entryInfoList({QStringLiteral("*.remove")}, QDir::Files);
    for (const auto &marker : removalMarkers) {
        const QString targetName = marker.completeBaseName();
        QFile::remove(directory.filePath(targetName));
        QFile::remove(marker.absoluteFilePath());
    }

    const auto pendingFiles = directory.entryInfoList({QStringLiteral("*.pending")}, QDir::Files);
    for (const auto &pending : pendingFiles) {
        const QString targetName = pending.completeBaseName();
        const QString target = directory.filePath(targetName);
        QFile::remove(target);
        QFile::rename(pending.absoluteFilePath(), target);
    }
}
