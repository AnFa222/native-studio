#include "extensionmanager.h"

#include "mainwindow.h"
#include "projectexplorer.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLibrary>
#include <QMenu>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QPointer>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

namespace {
const QSet<QString> &retiredBundledExtensionBases()
{
    static const QSet<QString> bases{
        QStringLiteral("native_hello"),
        QStringLiteral("native_python_variable_explorer")
    };
    return bases;
}

bool isRetiredBundledManifest(const QFileInfo &manifest)
{
    return retiredBundledExtensionBases().contains(manifest.completeBaseName().toLower());
}

void cleanRetiredBundledExtensions(const QString &directoryPath)
{
    QDir directory(directoryPath);
    for (const QString &base : retiredBundledExtensionBases()) {
        for (const QString &suffix : {QStringLiteral(".json"), QStringLiteral(".dll"),
                                      QStringLiteral(".so"), QStringLiteral(".dylib")})
            QFile::remove(directory.filePath(base + suffix));
    }
}
}

struct ExtensionManager::LoadedExtension
{
    ExtensionManager *manager = nullptr;
    NsHostApi hostApi{};
    std::unique_ptr<QLibrary> library;
    const NsExtensionDescriptor *descriptor = nullptr;
    NativeExtensionInfo info;
};

struct ExtensionManager::RegisteredCommand
{
    LoadedExtension *owner = nullptr;
    QString id;
    QAction *action = nullptr;
    NsCommandCallback callback = nullptr;
    void *userData = nullptr;
};

struct ExtensionManager::RegisteredEvent
{
    LoadedExtension *owner = nullptr;
    uint32_t type = 0;
    NsEventCallback callback = nullptr;
    void *userData = nullptr;
};

struct ExtensionManager::RegisteredSettingsPage
{
    struct Setting {
        QString key;
        QString label;
        QString description;
        uint32_t type = 0;
        QString defaultValue;
        int minimum = 0;
        int maximum = 0;
        QList<QPair<QString, QString>> choices;
    };

    LoadedExtension *owner = nullptr;
    QString title;
    QString description;
    QStringList keywords;
    QList<Setting> settings;
};

struct ExtensionManager::RegisteredView
{
    LoadedExtension *owner = nullptr;
    QString id;
    QString title;
    QString dockObjectName;
    uint32_t defaultLocation = NS_VIEW_LOCATION_LEFT;
    NsCreateViewCallback create = nullptr;
    NsViewProjectChangedCallback projectChanged = nullptr;
    void *userData = nullptr;
    QPointer<QWidget> widget;
};

ExtensionManager::ExtensionManager(MainWindow *window, QMenu *extensionsMenu, QObject *parent)
    : QObject(parent), m_window(window), m_extensionsMenu(extensionsMenu)
{
    m_hostApi.struct_size = sizeof(NsHostApi);
    m_hostApi.abi_version = NS_EXTENSION_ABI_VERSION;
    m_hostApi.host_context = this;
    m_hostApi.register_command = &ExtensionManager::hostRegisterCommand;
    m_hostApi.show_message = &ExtensionManager::hostShowMessage;
    m_hostApi.log = &ExtensionManager::hostLog;
    m_hostApi.open_file = &ExtensionManager::hostOpenFile;
    m_hostApi.insert_text = &ExtensionManager::hostInsertText;
    m_hostApi.replace_document_text = &ExtensionManager::hostReplaceDocumentText;
    m_hostApi.run_process = &ExtensionManager::hostRunProcess;
    m_hostApi.register_event_listener = &ExtensionManager::hostRegisterEventListener;
    m_hostApi.get_active_document_text = &ExtensionManager::hostGetActiveDocumentText;
    m_hostApi.register_settings_page = &ExtensionManager::hostRegisterSettingsPage;
    m_hostApi.get_extension_setting = &ExtensionManager::hostGetExtensionSetting;
    m_hostApi.show_view = &ExtensionManager::hostShowView;
    m_hostApi.register_view = &ExtensionManager::hostRegisterView;
    m_hostApi.set_file_decorations = &ExtensionManager::hostSetFileDecorations;
    m_hostApi.get_active_document_path = &ExtensionManager::hostGetActiveDocumentPath;
}

ExtensionManager::~ExtensionManager()
{
    shutdown();
}

QString ExtensionManager::fromView(NsStringView view)
{
    if (!view.data || view.size == 0) return {};
    const size_t bounded = std::min(view.size,
        static_cast<size_t>(std::numeric_limits<int>::max()));
    return QString::fromUtf8(view.data, static_cast<int>(bounded));
}

bool ExtensionManager::containsExtension(const QString &id) const
{
    return std::any_of(m_extensions.cbegin(), m_extensions.cend(), [&id](const auto &loaded) {
        return loaded->info.id.compare(id, Qt::CaseInsensitive) == 0;
    });
}

QStringList ExtensionManager::disabledExtensionIds()
{
    return QSettings().value(QStringLiteral("extensions/disabled")).toStringList();
}

void ExtensionManager::setDisabledExtensionIds(const QStringList &ids)
{
    QSettings().setValue(QStringLiteral("extensions/disabled"), ids);
}

QString ExtensionManager::bundledExtensionDirectory()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("extensions/bundled"));
}

QString ExtensionManager::multiConfigExtensionDirectory()
{
    return QDir(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("..")))
        .filePath(QStringLiteral("extensions/bundled"));
}

QString ExtensionManager::safeInstallBase(const QString &id)
{
    QString result = id;
    result.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return result.left(128);
}

bool ExtensionManager::readManifest(const QString &path, bool bundled,
                                    NativeExtensionInfo *info, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QObject::tr("Invalid JSON manifest: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("manifestVersion")).toInt() != 1) {
        if (error) *error = QObject::tr("Unsupported manifestVersion; expected 1.");
        return false;
    }
    const QString id = object.value(QStringLiteral("id")).toString().trimmed();
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{2,127}$"));
    if (!validId.match(id).hasMatch()) {
        if (error) *error = QObject::tr("Extension id is missing or invalid.");
        return false;
    }
    static const QSet<QString> supportedPermissions{
        QStringLiteral("ui.commands"), QStringLiteral("editor.read"),
        QStringLiteral("editor.write"), QStringLiteral("editor.events"),
        QStringLiteral("filesystem.read"), QStringLiteral("process.execute"),
        QStringLiteral("settings"), QStringLiteral("ui.views"),
        QStringLiteral("ui.filedecorations")
    };
    const QJsonValue permissionValue = object.value(QStringLiteral("permissions"));
    if (!permissionValue.isUndefined() && !permissionValue.isArray()) {
        if (error) *error = QObject::tr("Manifest permissions must be an array of strings.");
        return false;
    }
    for (const QJsonValue &permission : permissionValue.toArray()) {
        const QString value = permission.toString().trimmed().toLower();
        if (value.isEmpty() || !supportedPermissions.contains(value)) {
            if (error) *error = QObject::tr("Unknown extension permission: %1").arg(value);
            return false;
        }
    }
    const uint32_t abiVersion = static_cast<uint32_t>(
        object.value(QStringLiteral("abiVersion")).toInt());
    if (abiVersion != NS_EXTENSION_ABI_VERSION) {
        if (error) *error = QObject::tr("Extension requires ABI %1; this build supports ABI %2.")
            .arg(abiVersion).arg(NS_EXTENSION_ABI_VERSION);
        return false;
    }

    QString binaryName;
    const QJsonValue binary = object.value(QStringLiteral("binary"));
    if (binary.isString()) {
        binaryName = binary.toString();
    } else if (binary.isObject()) {
#ifdef Q_OS_WIN
        binaryName = binary.toObject().value(QStringLiteral("windows")).toString();
#elif defined(Q_OS_MACOS)
        binaryName = binary.toObject().value(QStringLiteral("macos")).toString();
#else
        binaryName = binary.toObject().value(QStringLiteral("linux")).toString();
#endif
    }
    binaryName = QDir::cleanPath(QDir::fromNativeSeparators(binaryName.trimmed()));
    if (binaryName.isEmpty() || QDir::isAbsolutePath(binaryName)
        || binaryName == QStringLiteral("..") || binaryName.startsWith(QStringLiteral("../"))) {
        if (error) *error = QObject::tr("Manifest binary must be a safe relative path.");
        return false;
    }
    const QFileInfo manifestInfo(path);
    const QFileInfo binaryInfo(manifestInfo.dir().filePath(binaryName));
    if (!binaryInfo.isFile() || !QLibrary::isLibrary(binaryInfo.fileName())) {
        if (error) *error = QObject::tr("Extension binary is missing or unsupported: %1")
            .arg(binaryName);
        return false;
    }
    const QString root = QDir::fromNativeSeparators(manifestInfo.dir().canonicalPath())
        + QLatin1Char('/');
    const QString canonicalBinary = QDir::fromNativeSeparators(binaryInfo.canonicalFilePath());
    if (!canonicalBinary.startsWith(root, Qt::CaseInsensitive)) {
        if (error) *error = QObject::tr("Extension binary escapes the manifest directory.");
        return false;
    }

    if (info) {
        const QStringList disabled = disabledExtensionIds();
        info->id = id;
        info->name = object.value(QStringLiteral("name")).toString(id).trimmed();
        info->version = object.value(QStringLiteral("version")).toString().trimmed();
        info->description = object.value(QStringLiteral("description")).toString().trimmed();
        info->author = object.value(QStringLiteral("author")).toString().trimmed();
        const QJsonArray permissionValues = object.value(QStringLiteral("permissions")).toArray();
        for (const QJsonValue &permission : permissionValues) {
            const QString value = permission.toString().trimmed().toLower();
            if (!value.isEmpty() && !info->permissions.contains(value))
                info->permissions.push_back(value);
        }
        info->filePath = binaryInfo.absoluteFilePath();
        info->manifestPath = manifestInfo.absoluteFilePath();
        info->abiVersion = abiVersion;
        info->bundled = bundled;
        info->enabled = !disabled.contains(id, Qt::CaseInsensitive);
        info->pendingRestart = QFileInfo::exists(info->filePath + QStringLiteral(".pending"))
            || QFileInfo::exists(info->manifestPath + QStringLiteral(".pending"))
            || QFileInfo::exists(QDir(userExtensionDirectory()).filePath(
                safeInstallBase(id) + QStringLiteral(".restart")))
            || QFileInfo::exists(manifestInfo.dir().filePath(safeInstallBase(id)
                                                             + QStringLiteral(".remove")));
    }
    return true;
}

QList<NativeExtensionInfo> ExtensionManager::manifestsInDirectory(const QString &directory,
                                                                  bool bundled) const
{
    QList<NativeExtensionInfo> result;
    QDir dir(directory);
    const auto manifests = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::Readable,
                                              QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &manifest : manifests) {
        if (bundled && isRetiredBundledManifest(manifest)) continue;
        NativeExtensionInfo info;
        if (!readManifest(manifest.absoluteFilePath(), bundled, &info)) continue;
        info.loaded = containsExtension(info.id);
        result.push_back(std::move(info));
    }
    return result;
}

bool ExtensionManager::loadManifest(const QString &manifestPath, bool bundled)
{
    if (m_shuttingDown) return false;
    NativeExtensionInfo manifest;
    if (!readManifest(manifestPath, bundled, &manifest)
        || !manifest.enabled || containsExtension(manifest.id)) return false;
    const QFileInfo file(manifest.filePath);
    auto extension = std::make_unique<LoadedExtension>();
    extension->manager = this;
    extension->hostApi = m_hostApi;
    extension->hostApi.host_context = extension.get();
    extension->library = std::make_unique<QLibrary>(file.absoluteFilePath());
    if (!extension->library->load()) {
        qWarning().noquote() << "Could not load native extension" << file.fileName()
                             << extension->library->errorString();
        return false;
    }

    const auto entry = reinterpret_cast<NsExtensionEntryFunction>(
        extension->library->resolve(NS_EXTENSION_ENTRY_SYMBOL));
    if (!entry) {
        extension->library->unload();
        return false;
    }

    const NsExtensionDescriptor *descriptor = nullptr;
    try {
        descriptor = entry();
    } catch (...) {
        qWarning() << "Native extension entry point threw an exception:" << file.fileName();
    }
    const size_t requiredSize = offsetof(NsExtensionDescriptor, deactivate)
        + sizeof(NsDeactivateCallback);
    if (!descriptor || descriptor->struct_size < requiredSize
        || descriptor->abi_version != NS_EXTENSION_ABI_VERSION || !descriptor->activate) {
        qWarning() << "Native extension has an incompatible descriptor:" << file.fileName();
        extension->library->unload();
        return false;
    }

    const QString id = fromView(descriptor->id).trimmed();
    const QString descriptorVersion = fromView(descriptor->version).trimmed();
    if (id.compare(manifest.id, Qt::CaseSensitive) != 0
        || (!manifest.version.isEmpty() && descriptorVersion != manifest.version)) {
        qWarning() << "Native extension descriptor does not match its manifest:" << file.fileName();
        extension->library->unload();
        return false;
    }
    extension->descriptor = descriptor;
    extension->info = manifest;
    extension->info.loaded = true;
    if (extension->info.name.isEmpty()) extension->info.name = id;

    LoadedExtension *handle = extension.get();
    m_extensions.push_back(std::move(extension));
    int32_t activationResult = -1;
    try {
        activationResult = descriptor->activate(descriptor->extension_context, &handle->hostApi,
                                                 static_cast<NsExtensionHandle>(handle));
    } catch (...) {
        qWarning() << "Native extension activation threw an exception:" << id;
    }
    if (activationResult != 0) {
        removeCommands(handle);
        removeEvents(handle);
        removeSettingsPages(handle);
        removeViews(handle);
        if (descriptor->deactivate) {
            try {
                descriptor->deactivate(descriptor->extension_context);
            } catch (...) {
                qWarning() << "Native extension cleanup threw an exception:" << id;
            }
        }
        handle->library->unload();
        m_extensions.pop_back();
        return false;
    }
    emit extensionLoaded(id);
    return true;
}

int ExtensionManager::loadExtensions(const QString &directory, bool bundled)
{
    if (m_shuttingDown) return 0;
    const QDir extensionDirectory(directory);
    if (!extensionDirectory.exists()) return 0;

    int loadedCount = 0;
    const auto manifests = extensionDirectory.entryInfoList(
        {QStringLiteral("*.json")}, QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &manifest : manifests) {
        if (bundled && isRetiredBundledManifest(manifest)) continue;
        if (loadManifest(manifest.absoluteFilePath(), bundled)) ++loadedCount;
    }
    return loadedCount;
}

void ExtensionManager::queueManifestDirectory(const QString &directory, bool bundled)
{
    const QDir extensionDirectory(directory);
    const auto manifests = extensionDirectory.entryInfoList(
        {QStringLiteral("*.json")}, QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &manifest : manifests) {
        if (bundled && isRetiredBundledManifest(manifest)) continue;
        m_pendingManifests.push_back({manifest.absoluteFilePath(), bundled});
    }
}

void ExtensionManager::loadDefaultExtensionsAsync()
{
    if (m_asyncLoading || m_shuttingDown) return;
    QDir().mkpath(userExtensionDirectory());
    applyPendingOperations();
    m_pendingManifests.clear();
    m_asyncLoadedCount = 0;
    const QString bundled = bundledExtensionDirectory();
    cleanRetiredBundledExtensions(bundled);
    queueManifestDirectory(bundled, true);
    const QString multiConfig = multiConfigExtensionDirectory();
    if (QDir::cleanPath(multiConfig) != QDir::cleanPath(bundled)) {
        cleanRetiredBundledExtensions(multiConfig);
        queueManifestDirectory(multiConfig, true);
    }
    queueManifestDirectory(userExtensionDirectory(), false);
    m_asyncLoading = true;
    QTimer::singleShot(0, this, &ExtensionManager::loadNextManifest);
}

void ExtensionManager::loadNextManifest()
{
    if (m_pendingManifests.isEmpty() || m_shuttingDown) {
        m_asyncLoading = false;
        emit loadingFinished(m_asyncLoadedCount);
        return;
    }
    const PendingManifest pending = m_pendingManifests.takeFirst();
    if (loadManifest(pending.filePath, pending.bundled)) ++m_asyncLoadedCount;
    QTimer::singleShot(0, this, &ExtensionManager::loadNextManifest);
}

int ExtensionManager::loadDefaultExtensions()
{
    QDir().mkpath(userExtensionDirectory());
    applyPendingOperations();
    int loaded = 0;
    const QString bundled = bundledExtensionDirectory();
    cleanRetiredBundledExtensions(bundled);
    loaded += loadExtensions(bundled, true);
    const QString multiConfig = multiConfigExtensionDirectory();
    if (QDir::cleanPath(multiConfig) != QDir::cleanPath(bundled)) {
        cleanRetiredBundledExtensions(multiConfig);
        loaded += loadExtensions(multiConfig, true);
    }
    loaded += loadExtensions(userExtensionDirectory(), false);
    return loaded;
}

void ExtensionManager::shutdown()
{
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    for (auto iterator = m_extensions.rbegin(); iterator != m_extensions.rend(); ++iterator) {
        LoadedExtension *extension = iterator->get();
        removeCommands(extension);
        removeEvents(extension);
        removeSettingsPages(extension);
        removeViews(extension);
        if (extension->descriptor && extension->descriptor->deactivate) {
            try {
                extension->descriptor->deactivate(extension->descriptor->extension_context);
            } catch (...) {
                qWarning() << "Native extension deactivation threw an exception:"
                           << extension->info.id;
            }
        }
        extension->library->unload();
    }
    m_extensions.clear();
    m_commands.clear();
    m_events.clear();
    m_settingsPages.clear();
    m_views.clear();
}

QList<NativeExtensionInfo> ExtensionManager::loadedExtensions() const
{
    QList<NativeExtensionInfo> result;
    result.reserve(static_cast<qsizetype>(m_extensions.size()));
    for (const auto &extension : m_extensions) result.push_back(extension->info);
    return result;
}

QList<NativeExtensionInfo> ExtensionManager::installedExtensions() const
{
    QList<NativeExtensionInfo> result;
    QSet<QString> ids;
    const auto appendUnique = [&result, &ids](const QList<NativeExtensionInfo> &items) {
        for (const NativeExtensionInfo &item : items) {
            const QString key = item.id.toLower();
            if (ids.contains(key)) continue;
            ids.insert(key);
            result.push_back(item);
        }
    };
    appendUnique(manifestsInDirectory(bundledExtensionDirectory(), true));
    if (QDir::cleanPath(multiConfigExtensionDirectory())
        != QDir::cleanPath(bundledExtensionDirectory()))
        appendUnique(manifestsInDirectory(multiConfigExtensionDirectory(), true));
    appendUnique(manifestsInDirectory(userExtensionDirectory(), false));
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool ExtensionManager::importExtension(const QString &manifestPath, QString *error,
                                       bool *restartRequired)
{
    if (restartRequired) *restartRequired = false;
    NativeExtensionInfo source;
    if (!readManifest(manifestPath, false, &source, error)) return false;
    const auto bundled = manifestsInDirectory(bundledExtensionDirectory(), true)
        + manifestsInDirectory(multiConfigExtensionDirectory(), true);
    if (std::any_of(bundled.cbegin(), bundled.cend(), [&source](const auto &extension) {
            return extension.id.compare(source.id, Qt::CaseInsensitive) == 0;
        })) {
        if (error) *error = tr("Bundled extensions cannot be replaced or updated.");
        return false;
    }

    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        if (error) *error = manifestFile.errorString();
        return false;
    }
    QJsonDocument manifestDocument = QJsonDocument::fromJson(manifestFile.readAll());
    QJsonObject normalized = manifestDocument.object();
    const QString base = safeInstallBase(source.id);
    const QString extension = QFileInfo(source.filePath).suffix();
    const QString targetBinaryName = base + QLatin1Char('.') + extension;
    const QString targetManifestName = base + QStringLiteral(".json");
    normalized.insert(QStringLiteral("binary"), targetBinaryName);

    QDir destination(userExtensionDirectory());
    destination.mkpath(QStringLiteral("."));
    const QString targetBinary = destination.filePath(targetBinaryName);
    const QString targetManifest = destination.filePath(targetManifestName);
    const bool requiresRestart = containsExtension(source.id)
        || QFileInfo::exists(targetBinary) || QFileInfo::exists(targetManifest);
    const QString binaryDestination = targetBinary
        + (requiresRestart ? QStringLiteral(".pending") : QString{});
    const QString manifestDestination = targetManifest
        + (requiresRestart ? QStringLiteral(".pending") : QString{});

    QFile::remove(binaryDestination);
    if (!QFile::copy(source.filePath, binaryDestination)) {
        if (error) *error = tr("Could not copy the extension binary.");
        return false;
    }
    QSaveFile savedManifest(manifestDestination);
    if (!savedManifest.open(QIODevice::WriteOnly)
        || savedManifest.write(QJsonDocument(normalized).toJson(QJsonDocument::Indented)) < 0
        || !savedManifest.commit()) {
        QFile::remove(binaryDestination);
        if (error) *error = tr("Could not install the extension manifest.");
        return false;
    }

    QStringList disabled = disabledExtensionIds();
    disabled.removeIf([&source](const QString &id) {
        return id.compare(source.id, Qt::CaseInsensitive) == 0;
    });
    setDisabledExtensionIds(disabled);

    if (requiresRestart) {
        QFile restartMarker(destination.filePath(base + QStringLiteral(".restart")));
        restartMarker.open(QIODevice::WriteOnly);
        restartMarker.close();
        if (restartRequired) *restartRequired = true;
        return true;
    }

    if (loadExtensions(userExtensionDirectory(), false) == 0 || !containsExtension(source.id)) {
        QFile::remove(targetBinary);
        QFile::remove(targetManifest);
        if (error) *error = tr("The binary did not export a matching ABI descriptor.");
        return false;
    }
    return true;
}

bool ExtensionManager::setExtensionEnabled(const QString &id, bool enabled, QString *error)
{
    const auto installed = installedExtensions();
    const auto found = std::find_if(installed.cbegin(), installed.cend(), [&id](const auto &item) {
        return item.id.compare(id, Qt::CaseInsensitive) == 0;
    });
    if (found == installed.cend()) {
        if (error) *error = tr("Extension is not installed.");
        return false;
    }
    QStringList disabled = disabledExtensionIds();
    disabled.removeIf([&id](const QString &entry) {
        return entry.compare(id, Qt::CaseInsensitive) == 0;
    });
    if (!enabled) disabled.push_back(found->id);
    setDisabledExtensionIds(disabled);
    QFile marker(QDir(userExtensionDirectory()).filePath(
        safeInstallBase(found->id) + QStringLiteral(".restart")));
    marker.open(QIODevice::WriteOnly);
    marker.close();
    return true;
}

bool ExtensionManager::removeExtension(const QString &id, QString *error)
{
    const auto installed = installedExtensions();
    const auto found = std::find_if(installed.cbegin(), installed.cend(), [&id](const auto &item) {
        return item.id.compare(id, Qt::CaseInsensitive) == 0;
    });
    if (found == installed.cend()) {
        if (error) *error = tr("Extension is not installed.");
        return false;
    }
    if (found->bundled) {
        if (error) *error = tr("Bundled extensions can be disabled but not removed.");
        return false;
    }

    QDir directory(userExtensionDirectory());
    const QString base = safeInstallBase(found->id);
    const auto stageRemoval = [&]() {
        QJsonObject removal;
        removal.insert(QStringLiteral("manifest"), QFileInfo(found->manifestPath).fileName());
        removal.insert(QStringLiteral("binary"), QFileInfo(found->filePath).fileName());
        QSaveFile marker(directory.filePath(base + QStringLiteral(".remove")));
        if (!marker.open(QIODevice::WriteOnly)
            || marker.write(QJsonDocument(removal).toJson(QJsonDocument::Compact)) < 0
            || !marker.commit()) {
            if (error) *error = tr("Could not schedule extension removal.");
            return false;
        }
        return true;
    };

    bool unloaded = true;
    const auto loaded = std::find_if(m_extensions.begin(), m_extensions.end(),
        [&id](const auto &extension) {
            return extension->info.id.compare(id, Qt::CaseInsensitive) == 0;
        });
    if (loaded != m_extensions.end()) {
        LoadedExtension *owner = loaded->get();
        removeCommands(owner);
        removeEvents(owner);
        removeSettingsPages(owner);
        removeViews(owner);
        if (owner->descriptor && owner->descriptor->deactivate) {
            try {
                owner->descriptor->deactivate(owner->descriptor->extension_context);
            } catch (...) {
                qWarning() << "Native extension deactivation threw an exception:" << id;
            }
        }
        unloaded = owner->library->unload();
        m_extensions.erase(loaded);
    }

    const bool manifestRemoved = !QFileInfo::exists(found->manifestPath)
        || QFile::remove(found->manifestPath);
    const bool binaryRemoved = unloaded && (!QFileInfo::exists(found->filePath)
        || QFile::remove(found->filePath));
    if ((!manifestRemoved || !binaryRemoved) && !stageRemoval()) return false;

    QStringList disabled = disabledExtensionIds();
    disabled.removeIf([&found](const QString &entry) {
        return entry.compare(found->id, Qt::CaseInsensitive) == 0;
    });
    setDisabledExtensionIds(disabled);
    return true;
}

void ExtensionManager::applyPendingOperations()
{
    QDir directory(userExtensionDirectory());
    directory.mkpath(QStringLiteral("."));
    const auto safeLeaf = [](const QString &name) {
        return !name.isEmpty() && QFileInfo(name).fileName() == name
            && name != QStringLiteral(".") && name != QStringLiteral("..");
    };
    const auto removalMarkers = directory.entryInfoList({QStringLiteral("*.remove")}, QDir::Files);
    for (const QFileInfo &markerInfo : removalMarkers) {
        QFile marker(markerInfo.absoluteFilePath());
        if (marker.open(QIODevice::ReadOnly)) {
            const QJsonObject removal = QJsonDocument::fromJson(marker.readAll()).object();
            for (const QString &key : {QStringLiteral("manifest"), QStringLiteral("binary")}) {
                const QString name = removal.value(key).toString();
                if (safeLeaf(name)) {
                    QFile::remove(directory.filePath(name));
                    QFile::remove(directory.filePath(name + QStringLiteral(".pending")));
                }
            }
        }
        QFile::remove(markerInfo.absoluteFilePath());
    }

    const auto pendingFiles = directory.entryInfoList({QStringLiteral("*.pending")}, QDir::Files,
                                                       QDir::Name);
    for (const QFileInfo &pending : pendingFiles) {
        QString targetName = pending.fileName();
        targetName.chop(QStringLiteral(".pending").size());
        if (!safeLeaf(targetName)) continue;
        const QString target = directory.filePath(targetName);
        QFile::remove(target);
        QFile::rename(pending.absoluteFilePath(), target);
    }
    for (const QFileInfo &marker : directory.entryInfoList({QStringLiteral("*.restart")}, QDir::Files))
        QFile::remove(marker.absoluteFilePath());
}

QStringList ExtensionManager::commandIds() const
{
    QStringList result;
    result.reserve(static_cast<qsizetype>(m_commands.size()));
    for (const auto &command : m_commands) result.push_back(command->id);
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList ExtensionManager::viewIds() const
{
    QStringList result;
    result.reserve(static_cast<qsizetype>(m_views.size()));
    for (const auto &view : m_views) result.push_back(view->id);
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool ExtensionManager::viewInfo(const QString &viewId, ExtensionViewInfo *result) const
{
    if (!result) return false;
    const auto iterator = std::find_if(m_views.cbegin(), m_views.cend(), [&viewId](const auto &view) {
        return view->id.compare(viewId, Qt::CaseSensitive) == 0;
    });
    if (iterator == m_views.cend()) return false;
    const RegisteredView *view = iterator->get();
    *result = {view->id, view->title, view->dockObjectName, view->defaultLocation};
    return true;
}

QWidget *ExtensionManager::createView(const QString &viewId, QWidget *parent,
                                      const QString &projectRoot)
{
    const auto iterator = std::find_if(m_views.begin(), m_views.end(), [&viewId](const auto &view) {
        return view->id.compare(viewId, Qt::CaseSensitive) == 0;
    });
    if (iterator == m_views.end() || (*iterator)->widget || !(*iterator)->create) return nullptr;
    RegisteredView *view = iterator->get();
    const QByteArray root = projectRoot.toUtf8();
    void *created = nullptr;
    try {
        created = view->create(view->userData, parent,
            {root.constData(), static_cast<size_t>(root.size())});
    } catch (...) {
        qWarning() << "Native extension view factory threw an exception:" << viewId;
    }
    auto *widget = static_cast<QWidget *>(created);
    if (!widget) return nullptr;
    if (widget->parentWidget() != parent) widget->setParent(parent);
    view->widget = widget;
    return widget;
}

void ExtensionManager::notifyProjectChanged(const QString &projectRoot)
{
    const QByteArray root = projectRoot.toUtf8();
    for (const auto &view : m_views) {
        if (!view->widget || !view->projectChanged) continue;
        try {
            view->projectChanged(view->userData, view->widget.data(),
                {root.constData(), static_cast<size_t>(root.size())});
        } catch (...) {
            qWarning() << "Native extension project callback threw an exception:" << view->id;
        }
    }
}

QList<ExtensionSettingsPageInfo> ExtensionManager::createSettingsPages(QWidget *parent) const
{
    QList<ExtensionSettingsPageInfo> result;
    for (const auto &registered : m_settingsPages) {
        if (!registered->owner) continue;
        auto *page = new QWidget(parent);
        page->setObjectName(QStringLiteral("extensionSettingsPage.%1")
                                .arg(registered->owner->info.id));
        auto *layout = new QVBoxLayout(page);
        auto *title = new QLabel(registered->title, page);
        title->setProperty("settingsHeading", true);
        layout->addWidget(title);
        if (!registered->description.isEmpty()) {
            auto *description = new QLabel(registered->description, page);
            description->setWordWrap(true);
            layout->addWidget(description);
        }
        auto *form = new QFormLayout;
        QList<std::function<void()>> applyActions;
        QList<std::function<void()>> resetActions;
        QSettings persisted;
        for (const auto &setting : registered->settings) {
            const QString path = QStringLiteral("extensions/settings/%1/%2")
                                     .arg(registered->owner->info.id, setting.key);
            const QVariant current = persisted.value(path, setting.defaultValue);
            const QString objectName = QStringLiteral("extensionSetting.%1.%2")
                                           .arg(registered->owner->info.id, setting.key);
            if (setting.type == NS_SETTING_BOOLEAN) {
                auto *control = new QCheckBox(setting.label, page);
                control->setObjectName(objectName);
                control->setToolTip(setting.description);
                const bool defaultValue = setting.defaultValue.compare(
                    QStringLiteral("true"), Qt::CaseInsensitive) == 0;
                const QString currentValue = current.toString();
                control->setChecked(currentValue.compare(
                    QStringLiteral("true"), Qt::CaseInsensitive) == 0
                    || currentValue == QStringLiteral("1"));
                form->addRow(QString{}, control);
                applyActions.push_back([control, path] {
                    QSettings().setValue(path, control->isChecked() ? QStringLiteral("true")
                                                                    : QStringLiteral("false"));
                });
                resetActions.push_back([control, defaultValue] {
                    control->setChecked(defaultValue);
                });
            } else if (setting.type == NS_SETTING_INTEGER) {
                auto *control = new QSpinBox(page);
                control->setObjectName(objectName);
                control->setToolTip(setting.description);
                control->setRange(setting.minimum, setting.maximum);
                const int defaultValue = setting.defaultValue.toInt();
                control->setValue(current.toInt());
                form->addRow(setting.label + QLatin1Char(':'), control);
                applyActions.push_back([control, path] {
                    QSettings().setValue(path, QString::number(control->value()));
                });
                resetActions.push_back([control, defaultValue] {
                    control->setValue(defaultValue);
                });
            } else if (setting.type == NS_SETTING_CHOICE) {
                auto *control = new QComboBox(page);
                control->setObjectName(objectName);
                control->setToolTip(setting.description);
                for (const auto &choice : setting.choices)
                    control->addItem(choice.second, choice.first);
                const int defaultIndex = qMax(0, control->findData(setting.defaultValue));
                const int currentIndex = control->findData(current.toString());
                control->setCurrentIndex(currentIndex >= 0 ? currentIndex : defaultIndex);
                form->addRow(setting.label + QLatin1Char(':'), control);
                applyActions.push_back([control, path] {
                    QSettings().setValue(path, control->currentData().toString());
                });
                resetActions.push_back([control, defaultIndex] {
                    control->setCurrentIndex(defaultIndex);
                });
            } else {
                auto *control = new QLineEdit(current.toString(), page);
                control->setObjectName(objectName);
                control->setToolTip(setting.description);
                const QString defaultValue = setting.defaultValue;
                form->addRow(setting.label + QLatin1Char(':'), control);
                applyActions.push_back([control, path] {
                    QSettings().setValue(path, control->text());
                });
                resetActions.push_back([control, defaultValue] {
                    control->setText(defaultValue);
                });
            }
        }
        layout->addLayout(form);
        layout->addStretch();
        result.push_back({
            registered->title,
            registered->keywords,
            page,
            [applyActions] { for (const auto &apply : applyActions) apply(); },
            [resetActions] { for (const auto &reset : resetActions) reset(); }
        });
    }
    return result;
}

void ExtensionManager::notifyDocumentWillSave()
{
    if (m_shuttingDown || !m_window) return;
    const QByteArray activeFile = m_window->activeFilePath().toUtf8();
    for (const auto &listener : m_events) {
        if (listener->type != NS_EVENT_DOCUMENT_WILL_SAVE || !listener->callback) continue;
        const QByteArray documentText = m_window->activeDocumentText().toUtf8();
        const NsEventContext context{
            sizeof(NsEventContext),
            NS_EVENT_DOCUMENT_WILL_SAVE,
            {activeFile.constData(), static_cast<size_t>(activeFile.size())},
            {documentText.constData(), static_cast<size_t>(documentText.size())}
        };
        try {
            listener->callback(listener->userData, &context);
        } catch (...) {
            qWarning() << "Native extension event callback threw an exception";
        }
    }
}

QString ExtensionManager::userExtensionDirectory()
{
    const QString overridePath = qEnvironmentVariable("NATIVE_STUDIO_USER_EXTENSION_DIR").trimmed();
    if (!overridePath.isEmpty()) return QDir::cleanPath(overridePath);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("extensions"));
}

QMenu *ExtensionManager::commandMenu(const QString &category)
{
    if (!m_extensionsMenu || category.trimmed().isEmpty()) return m_extensionsMenu;
    const QString title = category.trimmed();
    for (QAction *action : m_extensionsMenu->actions()) {
        if (action->menu() && action->text().compare(title, Qt::CaseInsensitive) == 0)
            return action->menu();
    }
    return m_extensionsMenu->addMenu(title);
}

bool ExtensionManager::registerCommand(LoadedExtension *owner, const NsCommandSpec *command)
{
    const size_t requiredSize = offsetof(NsCommandSpec, user_data) + sizeof(void *);
    if (!owner || !command || command->struct_size < requiredSize || !command->callback)
        return false;
    const QString id = fromView(command->id).trimmed();
    const QString title = fromView(command->title).trimmed();
    if (id.isEmpty() || title.isEmpty()) return false;
    if (std::any_of(m_commands.cbegin(), m_commands.cend(), [&id](const auto &existing) {
            return existing->id.compare(id, Qt::CaseInsensitive) == 0;
        })) return false;

    QMenu *menu = commandMenu(fromView(command->category));
    if (!menu) return false;
    auto registered = std::make_unique<RegisteredCommand>();
    registered->owner = owner;
    registered->id = id;
    registered->callback = command->callback;
    registered->userData = command->user_data;
    registered->action = menu->addAction(title);
    registered->action->setObjectName(QStringLiteral("extensionCommand.%1").arg(id));
    const QString shortcut = fromView(command->default_shortcut).trimmed();
    const QString shortcutKey = QStringLiteral("keybindings/") + registered->action->objectName();
    if (QSettings().contains(shortcutKey))
        registered->action->setShortcut(QKeySequence(QSettings().value(shortcutKey).toString()));
    else if (!shortcut.isEmpty())
        registered->action->setShortcut(QKeySequence(shortcut));
    RegisteredCommand *raw = registered.get();
    connect(registered->action, &QAction::triggered, this, [this, raw] { triggerCommand(raw); });
    m_commands.push_back(std::move(registered));
    return true;
}

bool ExtensionManager::registerEvent(LoadedExtension *owner, uint32_t eventType,
                                     NsEventCallback callback, void *userData)
{
    if (!owner || !callback || eventType != NS_EVENT_DOCUMENT_WILL_SAVE) return false;
    auto listener = std::make_unique<RegisteredEvent>();
    listener->owner = owner;
    listener->type = eventType;
    listener->callback = callback;
    listener->userData = userData;
    m_events.push_back(std::move(listener));
    return true;
}

bool ExtensionManager::registerSettingsPage(LoadedExtension *owner,
                                            const NsSettingsPageSpec *page)
{
    const size_t requiredPageSize = offsetof(NsSettingsPageSpec, setting_count)
        + sizeof(page->setting_count);
    if (!owner || !page || page->struct_size < requiredPageSize
        || page->setting_count > 128 || (page->setting_count > 0 && !page->settings)) return false;
    if (std::any_of(m_settingsPages.cbegin(), m_settingsPages.cend(), [owner](const auto &existing) {
            return existing->owner == owner;
        })) return false;
    auto registered = std::make_unique<RegisteredSettingsPage>();
    registered->owner = owner;
    registered->title = fromView(page->title).trimmed();
    registered->description = fromView(page->description).trimmed();
    registered->keywords = fromView(page->keywords).split(
        QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    if (registered->title.isEmpty()) return false;

    static const QRegularExpression validKey(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    QSet<QString> keys;
    for (size_t index = 0; index < page->setting_count; ++index) {
        const NsSettingSpec &source = page->settings[index];
        const size_t requiredSettingSize = offsetof(NsSettingSpec, choice_count)
            + sizeof(source.choice_count);
        if (source.struct_size < requiredSettingSize || source.choice_count > 64
            || (source.choice_count > 0 && !source.choices)) return false;
        RegisteredSettingsPage::Setting setting;
        setting.key = fromView(source.key).trimmed();
        setting.label = fromView(source.label).trimmed();
        setting.description = fromView(source.description).trimmed();
        setting.type = source.type;
        setting.defaultValue = fromView(source.default_value);
        if (!validKey.match(setting.key).hasMatch() || setting.label.isEmpty()
            || keys.contains(setting.key.toLower())) return false;
        keys.insert(setting.key.toLower());
        if (setting.type == NS_SETTING_INTEGER) {
            const int64_t minimum = std::clamp<int64_t>(
                source.minimum, std::numeric_limits<int>::min(),
                std::numeric_limits<int>::max());
            const int64_t maximum = std::clamp<int64_t>(
                source.maximum, std::numeric_limits<int>::min(),
                std::numeric_limits<int>::max());
            if (minimum > maximum) return false;
            setting.minimum = static_cast<int>(minimum);
            setting.maximum = static_cast<int>(maximum);
            bool validDefault = false;
            const int defaultValue = setting.defaultValue.toInt(&validDefault);
            if (!validDefault || defaultValue < setting.minimum || defaultValue > setting.maximum)
                return false;
        } else if (setting.type == NS_SETTING_CHOICE) {
            for (size_t choice = 0; choice < source.choice_count; ++choice) {
                const size_t requiredChoiceSize = offsetof(NsSettingChoice, label)
                    + sizeof(source.choices[choice].label);
                if (source.choices[choice].struct_size < requiredChoiceSize) return false;
                const QString value = fromView(source.choices[choice].value);
                const QString label = fromView(source.choices[choice].label);
                if (value.isEmpty() || label.isEmpty()) return false;
                setting.choices.push_back({value, label});
            }
            if (setting.choices.isEmpty()) return false;
            const bool validDefault = std::any_of(
                setting.choices.cbegin(), setting.choices.cend(), [&setting](const auto &choice) {
                    return choice.first == setting.defaultValue;
                });
            if (!validDefault) return false;
        } else if (setting.type == NS_SETTING_BOOLEAN) {
            const QString lowered = setting.defaultValue.trimmed().toLower();
            if (lowered != QStringLiteral("true") && lowered != QStringLiteral("false"))
                return false;
            setting.defaultValue = lowered;
        } else if (setting.type != NS_SETTING_TEXT) {
            return false;
        }
        registered->settings.push_back(std::move(setting));
    }
    m_settingsPages.push_back(std::move(registered));
    return true;
}

bool ExtensionManager::registerView(LoadedExtension *owner, const NsViewSpec *view)
{
    const size_t requiredSize = offsetof(NsViewSpec, user_data) + sizeof(view->user_data);
    if (!owner || !view || view->struct_size < requiredSize || !view->create) return false;
    auto registered = std::make_unique<RegisteredView>();
    registered->owner = owner;
    registered->id = fromView(view->id).trimmed();
    registered->title = fromView(view->title).trimmed();
    registered->dockObjectName = fromView(view->dock_object_name).trimmed();
    registered->defaultLocation = view->default_location;
    registered->create = view->create;
    registered->projectChanged = view->project_changed;
    registered->userData = view->user_data;
    static const QRegularExpression validId(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
    if (!validId.match(registered->id).hasMatch() || registered->title.isEmpty()
        || !validId.match(registered->dockObjectName).hasMatch()
        || (registered->defaultLocation != NS_VIEW_LOCATION_LEFT
            && registered->defaultLocation != NS_VIEW_LOCATION_RIGHT
            && registered->defaultLocation != NS_VIEW_LOCATION_BOTTOM)
        || std::any_of(m_views.cbegin(), m_views.cend(), [&registered](const auto &existing) {
            return existing->id.compare(registered->id, Qt::CaseInsensitive) == 0
                || existing->dockObjectName.compare(registered->dockObjectName,
                                                     Qt::CaseInsensitive) == 0;
        })) return false;
    m_views.push_back(std::move(registered));
    return true;
}

QString ExtensionManager::extensionSetting(LoadedExtension *owner, const QString &key) const
{
    if (!owner) return {};
    for (const auto &page : m_settingsPages) {
        if (page->owner != owner) continue;
        for (const auto &setting : page->settings) {
            if (setting.key.compare(key, Qt::CaseInsensitive) != 0) continue;
            const QString path = QStringLiteral("extensions/settings/%1/%2")
                                     .arg(owner->info.id, setting.key);
            const QString stored = QSettings().value(path, setting.defaultValue).toString();
            if (setting.type == NS_SETTING_BOOLEAN) {
                const QString lowered = stored.trimmed().toLower();
                return lowered == QStringLiteral("true") || lowered == QStringLiteral("1")
                    ? QStringLiteral("true")
                    : lowered == QStringLiteral("false") || lowered == QStringLiteral("0")
                        ? QStringLiteral("false") : setting.defaultValue;
            }
            if (setting.type == NS_SETTING_INTEGER) {
                bool valid = false;
                const int value = stored.toInt(&valid);
                return valid && value >= setting.minimum && value <= setting.maximum
                    ? QString::number(value) : setting.defaultValue;
            }
            if (setting.type == NS_SETTING_CHOICE) {
                const bool valid = std::any_of(
                    setting.choices.cbegin(), setting.choices.cend(), [&stored](const auto &choice) {
                        return choice.first == stored;
                    });
                return valid ? stored : setting.defaultValue;
            }
            return stored;
        }
    }
    return {};
}

bool ExtensionManager::hasPermission(const LoadedExtension *owner, const QString &permission) const
{
    return owner && owner->info.permissions.contains(permission, Qt::CaseInsensitive);
}

void ExtensionManager::triggerCommand(RegisteredCommand *command)
{
    if (!command || !command->callback || m_shuttingDown) return;
    const QByteArray activeFile = m_window->activeFilePath().toUtf8();
    const QByteArray projectRoot = m_window->projectDirectory().toUtf8();
    const NsCommandContext context{
        sizeof(NsCommandContext),
        {activeFile.constData(), static_cast<size_t>(activeFile.size())},
        {projectRoot.constData(), static_cast<size_t>(projectRoot.size())},
        m_window->activeDocumentModified() ? 1u : 0u
    };
    try {
        command->callback(command->userData, &context);
    } catch (...) {
        qWarning() << "Native extension command threw an exception:" << command->id;
        m_window->showExtensionMessage(tr("Extension command failed: %1").arg(command->id), 5000);
    }
}

void ExtensionManager::removeCommands(LoadedExtension *owner)
{
    const auto firstRemoved = std::remove_if(m_commands.begin(), m_commands.end(),
        [owner](const auto &command) {
            if (command->owner != owner) return false;
            delete command->action;
            command->action = nullptr;
            return true;
        });
    m_commands.erase(firstRemoved, m_commands.end());
}

void ExtensionManager::removeEvents(LoadedExtension *owner)
{
    const auto firstRemoved = std::remove_if(m_events.begin(), m_events.end(),
        [owner](const auto &event) { return event->owner == owner; });
    m_events.erase(firstRemoved, m_events.end());
}

void ExtensionManager::removeSettingsPages(LoadedExtension *owner)
{
    const auto firstRemoved = std::remove_if(m_settingsPages.begin(), m_settingsPages.end(),
        [owner](const auto &page) { return page->owner == owner; });
    m_settingsPages.erase(firstRemoved, m_settingsPages.end());
}

void ExtensionManager::removeViews(LoadedExtension *owner)
{
    if (owner && m_window) m_window->setExtensionFileDecorations(owner->info.id, {});
    const auto firstRemoved = std::remove_if(m_views.begin(), m_views.end(),
        [owner](const auto &view) {
            if (view->owner != owner) return false;
            if (view->widget) {
                QWidget *container = view->widget->parentWidget();
                if (auto *dock = qobject_cast<QDockWidget *>(container)) delete dock;
                else delete view->widget.data();
            }
            return true;
        });
    m_views.erase(firstRemoved, m_views.end());
}

int32_t ExtensionManager::hostRegisterCommand(void *hostContext, NsExtensionHandle extension,
                                               const NsCommandSpec *command)
{
    auto *contextOwner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = contextOwner ? contextOwner->manager : nullptr;
    auto *owner = static_cast<LoadedExtension *>(extension);
    if (!manager || owner != contextOwner || manager->m_shuttingDown
        || !manager->hasPermission(owner, QStringLiteral("ui.commands"))) return 0;
    const bool knownOwner = std::any_of(manager->m_extensions.cbegin(), manager->m_extensions.cend(),
        [owner](const auto &candidate) { return candidate.get() == owner; });
    return knownOwner && manager->registerCommand(owner, command) ? 1 : 0;
}

void ExtensionManager::hostShowMessage(void *hostContext, NsStringView message, uint32_t timeoutMs)
{
    auto *owner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = owner ? owner->manager : nullptr;
    if (manager && manager->m_window)
        manager->m_window->showExtensionMessage(fromView(message), static_cast<int>(timeoutMs));
}

void ExtensionManager::hostLog(void *, NsLogLevel level, NsStringView message)
{
    const QString text = fromView(message);
    if (level == NS_LOG_ERROR) qCritical().noquote() << "[extension]" << text;
    else if (level == NS_LOG_WARNING) qWarning().noquote() << "[extension]" << text;
    else if (level == NS_LOG_DEBUG) qDebug().noquote() << "[extension]" << text;
    else qInfo().noquote() << "[extension]" << text;
}

int32_t ExtensionManager::hostOpenFile(void *hostContext, NsStringView path)
{
    auto *owner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = owner ? owner->manager : nullptr;
    if (!manager || !manager->m_window
        || !manager->hasPermission(owner, QStringLiteral("filesystem.read"))) return 0;
    const QString filePath = fromView(path);
    if (filePath.isEmpty()) return 0;
    manager->m_window->openFile(filePath);
    return 1;
}

int32_t ExtensionManager::hostInsertText(void *hostContext, NsStringView text)
{
    auto *owner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = owner ? owner->manager : nullptr;
    return manager && manager->m_window
        && manager->hasPermission(owner, QStringLiteral("editor.write"))
        && manager->m_window->insertExtensionText(fromView(text)) ? 1 : 0;
}

int32_t ExtensionManager::hostReplaceDocumentText(void *hostContext, NsStringView text)
{
    auto *owner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = owner ? owner->manager : nullptr;
    return manager && manager->m_window
        && manager->hasPermission(owner, QStringLiteral("editor.write"))
        && manager->m_window->replaceExtensionDocument(fromView(text)) ? 1 : 0;
}

int32_t ExtensionManager::hostRunProcess(void *hostContext, NsStringView program,
                                         const NsStringView *arguments, size_t argumentCount,
                                         NsStringView workingDirectory)
{
    auto *owner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = owner ? owner->manager : nullptr;
    if (!manager || !manager->m_window || argumentCount > 4096
        || (argumentCount > 0 && !arguments)
        || !manager->hasPermission(owner, QStringLiteral("process.execute"))) return 0;
    QStringList convertedArguments;
    convertedArguments.reserve(static_cast<qsizetype>(std::min(argumentCount,
        static_cast<size_t>(std::numeric_limits<qsizetype>::max()))));
    for (size_t index = 0; index < argumentCount; ++index)
        convertedArguments.push_back(fromView(arguments[index]));
    return manager->m_window->runExtensionProcess(fromView(program), convertedArguments,
                                                   fromView(workingDirectory)) ? 1 : 0;
}

int32_t ExtensionManager::hostRegisterEventListener(void *hostContext,
                                                    NsExtensionHandle extension,
                                                    uint32_t eventType,
                                                    NsEventCallback callback,
                                                    void *userData)
{
    auto *contextOwner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = contextOwner ? contextOwner->manager : nullptr;
    auto *owner = static_cast<LoadedExtension *>(extension);
    if (!manager || owner != contextOwner || manager->m_shuttingDown
        || !manager->hasPermission(owner, QStringLiteral("editor.events"))) return 0;
    const bool knownOwner = std::any_of(manager->m_extensions.cbegin(), manager->m_extensions.cend(),
        [owner](const auto &candidate) { return candidate.get() == owner; });
    return knownOwner && manager->registerEvent(owner, eventType, callback, userData) ? 1 : 0;
}

size_t ExtensionManager::hostGetActiveDocumentText(void *hostContext, char *buffer,
                                                   size_t capacity)
{
    auto *owner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = owner ? owner->manager : nullptr;
    if (!manager || !manager->m_window
        || !manager->hasPermission(owner, QStringLiteral("editor.read"))) return 0;
    const QByteArray text = manager->m_window->activeDocumentText().toUtf8();
    const size_t required = static_cast<size_t>(text.size());
    if (buffer && capacity >= required && required > 0)
        std::memcpy(buffer, text.constData(), required);
    return required;
}

size_t ExtensionManager::hostGetActiveDocumentPath(void *hostContext, char *buffer,
                                                   size_t capacity)
{
    auto *owner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = owner ? owner->manager : nullptr;
    if (!manager || !manager->m_window
        || !manager->hasPermission(owner, QStringLiteral("editor.read"))) return 0;
    const QByteArray path = manager->m_window->activeFilePath().toUtf8();
    const size_t required = static_cast<size_t>(path.size());
    if (buffer && capacity >= required && required > 0)
        std::memcpy(buffer, path.constData(), required);
    return required;
}

int32_t ExtensionManager::hostRegisterSettingsPage(void *hostContext,
                                                   NsExtensionHandle extension,
                                                   const NsSettingsPageSpec *page)
{
    auto *contextOwner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = contextOwner ? contextOwner->manager : nullptr;
    auto *owner = static_cast<LoadedExtension *>(extension);
    if (!manager || owner != contextOwner || manager->m_shuttingDown
        || !manager->hasPermission(owner, QStringLiteral("settings"))) return 0;
    const bool knownOwner = std::any_of(manager->m_extensions.cbegin(), manager->m_extensions.cend(),
        [owner](const auto &candidate) { return candidate.get() == owner; });
    return knownOwner && manager->registerSettingsPage(owner, page) ? 1 : 0;
}

size_t ExtensionManager::hostGetExtensionSetting(void *hostContext,
                                                 NsExtensionHandle extension,
                                                 NsStringView key,
                                                 char *buffer,
                                                 size_t capacity)
{
    auto *contextOwner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = contextOwner ? contextOwner->manager : nullptr;
    auto *owner = static_cast<LoadedExtension *>(extension);
    if (!manager || owner != contextOwner || manager->m_shuttingDown
        || !manager->hasPermission(owner, QStringLiteral("settings"))) return 0;
    const bool knownOwner = std::any_of(manager->m_extensions.cbegin(), manager->m_extensions.cend(),
        [owner](const auto &candidate) { return candidate.get() == owner; });
    if (!knownOwner) return 0;
    const QByteArray value = manager->extensionSetting(owner, fromView(key)).toUtf8();
    const size_t required = static_cast<size_t>(value.size());
    if (buffer && capacity >= required && required > 0)
        std::memcpy(buffer, value.constData(), required);
    return required;
}

int32_t ExtensionManager::hostShowView(void *hostContext, NsStringView viewId)
{
    auto *owner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = owner ? owner->manager : nullptr;
    return manager && manager->m_window
        && manager->hasPermission(owner, QStringLiteral("ui.views"))
        && manager->m_window->showExtensionView(fromView(viewId)) ? 1 : 0;
}

int32_t ExtensionManager::hostRegisterView(void *hostContext, NsExtensionHandle extension,
                                           const NsViewSpec *view)
{
    auto *contextOwner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = contextOwner ? contextOwner->manager : nullptr;
    auto *owner = static_cast<LoadedExtension *>(extension);
    if (!manager || owner != contextOwner || manager->m_shuttingDown
        || !manager->hasPermission(owner, QStringLiteral("ui.views"))) return 0;
    const bool knownOwner = std::any_of(manager->m_extensions.cbegin(), manager->m_extensions.cend(),
        [owner](const auto &candidate) { return candidate.get() == owner; });
    return knownOwner && manager->registerView(owner, view) ? 1 : 0;
}

int32_t ExtensionManager::hostSetFileDecorations(
    void *hostContext, NsExtensionHandle extension,
    const NsFileDecoration *decorations, size_t decorationCount)
{
    auto *contextOwner = static_cast<LoadedExtension *>(hostContext);
    auto *manager = contextOwner ? contextOwner->manager : nullptr;
    auto *owner = static_cast<LoadedExtension *>(extension);
    if (!manager || !manager->m_window || owner != contextOwner
        || decorationCount > 100000 || (decorationCount > 0 && !decorations)
        || !manager->hasPermission(owner, QStringLiteral("ui.fileDecorations"))) return 0;
    QList<ExplorerFileDecoration> converted;
    converted.reserve(static_cast<qsizetype>(decorationCount));
    for (size_t index = 0; index < decorationCount; ++index) {
        const NsFileDecoration &source = decorations[index];
        const size_t requiredSize = offsetof(NsFileDecoration, kind) + sizeof(source.kind);
        if (source.struct_size < requiredSize
            || source.kind < NS_FILE_DECORATION_MODIFIED
            || source.kind > NS_FILE_DECORATION_CONFLICT) return 0;
        const QString path = fromView(source.path).trimmed();
        if (path.isEmpty()) return 0;
        converted.push_back({path, fromView(source.label).trimmed(), source.kind});
    }
    manager->m_window->setExtensionFileDecorations(owner->info.id, converted);
    return 1;
}
