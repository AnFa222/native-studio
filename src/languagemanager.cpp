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
#include <QTimer>

LanguageManager::LanguageManager(QObject *parent)
    : QObject(parent)
{
}

LanguageManager::~LanguageManager()
{
    for (const auto &loaded : m_plugins) loaded.plugin->shutdown();
}

bool LanguageManager::loadPluginFile(const QString &filePath, bool bundled)
{
    const QFileInfo file(filePath);
    const auto disabled = QSettings().value(QStringLiteral("plugins/disabled")).toStringList();
    if (!file.isFile() || !file.isReadable()
        || disabled.contains(file.fileName(), Qt::CaseInsensitive)) return false;

    auto loader = std::make_unique<QPluginLoader>(file.absoluteFilePath());
    QObject *instance = loader->instance();
    auto *plugin = qobject_cast<LanguagePlugin *>(instance);
    if (!plugin) {
        loader->unload();
        return false;
    }

    for (const auto &existing : m_plugins) {
        if (existing.plugin->languageName() == plugin->languageName()) {
            loader->unload();
            return false;
        }
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
    const QString languageName = plugin->languageName();
    m_plugins.push_back({std::move(loader), instance, plugin, info});
    emit pluginLoaded(languageName);
    return true;
}

int LanguageManager::loadPlugins(const QString &directory, bool bundled)
{
    QDir pluginDirectory(directory);
    if (!pluginDirectory.exists()) return 0;

    int loadedCount = 0;
    const auto files = pluginDirectory.entryInfoList(QDir::Files | QDir::Readable);
    for (const auto &file : files) {
        if (loadPluginFile(file.absoluteFilePath(), bundled)) ++loadedCount;
    }
    return loadedCount;
}

int LanguageManager::loadDefaultPlugins()
{
    applyPendingOperations();
    int loaded = loadPlugins(userPluginDirectory(), false);
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QString bundledPath = applicationDirectory.filePath(QStringLiteral("plugins/languages"));
    loaded += loadPlugins(bundledPath, true);
    const QString multiConfigPath = QDir(applicationDirectory.filePath(QStringLiteral("..")))
                                        .filePath(QStringLiteral("plugins/languages"));
    if (QDir::cleanPath(multiConfigPath) != QDir::cleanPath(bundledPath)) {
        loaded += loadPlugins(multiConfigPath, true);
    }
    return loaded;
}

void LanguageManager::queuePluginDirectory(const QString &directory, bool bundled)
{
    const QDir pluginDirectory(directory);
    const auto files = pluginDirectory.entryInfoList(QDir::Files | QDir::Readable,
                                                      QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files)
        m_pendingPlugins.push_back({file.absoluteFilePath(), bundled});
}

void LanguageManager::loadDefaultPluginsAsync()
{
    if (m_asyncLoading) return;
    applyPendingOperations();
    m_pendingPlugins.clear();
    m_asyncLoadedCount = 0;
    queuePluginDirectory(userPluginDirectory(), false);
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QString bundledPath = applicationDirectory.filePath(QStringLiteral("plugins/languages"));
    queuePluginDirectory(bundledPath, true);
    const QString multiConfigPath = QDir(applicationDirectory.filePath(QStringLiteral("..")))
                                        .filePath(QStringLiteral("plugins/languages"));
    if (QDir::cleanPath(multiConfigPath) != QDir::cleanPath(bundledPath))
        queuePluginDirectory(multiConfigPath, true);
    m_asyncLoading = true;
    QTimer::singleShot(0, this, &LanguageManager::loadNextPlugin);
}

void LanguageManager::loadNextPlugin()
{
    if (m_pendingPlugins.isEmpty()) {
        m_asyncLoading = false;
        emit loadingFinished(m_asyncLoadedCount);
        return;
    }
    const PendingPlugin pending = m_pendingPlugins.takeFirst();
    if (loadPluginFile(pending.filePath, pending.bundled)) ++m_asyncLoadedCount;
    QTimer::singleShot(0, this, &LanguageManager::loadNextPlugin);
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

QStringList LanguageManager::completionItems(const QString &filePath,
                                             const QString &documentText) const
{
    const auto *plugin = m_byExtension.value(QFileInfo(filePath).suffix().toLower(), nullptr);
    return plugin ? plugin->completionItems(documentText) : QStringList{};
}

QList<CodeDiagnostic> LanguageManager::diagnostics(const QString &filePath,
                                                   const QString &documentText) const
{
    const auto *plugin = m_byExtension.value(QFileInfo(filePath).suffix().toLower(), nullptr);
    return plugin ? plugin->diagnostics(documentText) : QList<CodeDiagnostic>{};
}

QList<LanguageStatusWidgetInfo> LanguageManager::createStatusWidgets(QWidget *parent) const
{
    QList<LanguageStatusWidgetInfo> widgets;
    for (const auto &loaded : m_plugins) {
        auto *provider = qobject_cast<LanguageStatusProvider *>(loaded.instance);
        if (!provider) continue;
        if (QWidget *widget = provider->createStatusWidget(parent))
            widgets.push_back({widget, loaded.plugin->fileExtensions()});
    }
    return widgets;
}

QList<LanguageSettingsPageInfo> LanguageManager::createSettingsPages(QWidget *parent) const
{
    QList<LanguageSettingsPageInfo> pages;
    for (const auto &loaded : m_plugins) {
        auto *provider = qobject_cast<LanguageSettingsProvider *>(loaded.instance);
        if (!provider) continue;
        if (QWidget *widget = provider->createSettingsWidget(parent))
            pages.push_back({provider->settingsTitle(), provider->settingsKeywords(), widget});
    }
    return pages;
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
