#include "extensionmanager.h"

#include "mainwindow.h"

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLibrary>
#include <QMenu>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <cstddef>
#include <limits>

struct ExtensionManager::LoadedExtension
{
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
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("extensions"));
}

QString ExtensionManager::multiConfigExtensionDirectory()
{
    return QDir(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("..")))
        .filePath(QStringLiteral("extensions"));
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
        activationResult = descriptor->activate(descriptor->extension_context, &m_hostApi,
                                                 static_cast<NsExtensionHandle>(handle));
    } catch (...) {
        qWarning() << "Native extension activation threw an exception:" << id;
    }
    if (activationResult != 0) {
        removeCommands(handle);
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
    for (const QFileInfo &manifest : manifests)
        m_pendingManifests.push_back({manifest.absoluteFilePath(), bundled});
}

void ExtensionManager::loadDefaultExtensionsAsync()
{
    if (m_asyncLoading || m_shuttingDown) return;
    QDir().mkpath(userExtensionDirectory());
    applyPendingOperations();
    m_pendingManifests.clear();
    m_asyncLoadedCount = 0;
    queueManifestDirectory(userExtensionDirectory(), false);
    const QString bundled = bundledExtensionDirectory();
    queueManifestDirectory(bundled, true);
    const QString multiConfig = multiConfigExtensionDirectory();
    if (QDir::cleanPath(multiConfig) != QDir::cleanPath(bundled))
        queueManifestDirectory(multiConfig, true);
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
    int loaded = loadExtensions(userExtensionDirectory(), false);
    const QString bundled = bundledExtensionDirectory();
    loaded += loadExtensions(bundled, true);
    const QString multiConfig = multiConfigExtensionDirectory();
    if (QDir::cleanPath(multiConfig) != QDir::cleanPath(bundled))
        loaded += loadExtensions(multiConfig, true);
    return loaded;
}

void ExtensionManager::shutdown()
{
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    for (auto iterator = m_extensions.rbegin(); iterator != m_extensions.rend(); ++iterator) {
        LoadedExtension *extension = iterator->get();
        removeCommands(extension);
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
    appendUnique(manifestsInDirectory(userExtensionDirectory(), false));
    appendUnique(manifestsInDirectory(bundledExtensionDirectory(), true));
    if (QDir::cleanPath(multiConfigExtensionDirectory())
        != QDir::cleanPath(bundledExtensionDirectory()))
        appendUnique(manifestsInDirectory(multiConfigExtensionDirectory(), true));
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

    const QString base = safeInstallBase(found->id);
    QDir directory(userExtensionDirectory());
    if (found->loaded) {
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
    } else {
        if (!QFile::remove(found->manifestPath) || !QFile::remove(found->filePath)) {
            if (error) *error = tr("Could not remove extension files.");
            return false;
        }
    }
    QStringList disabled = disabledExtensionIds();
    if (!disabled.contains(found->id, Qt::CaseInsensitive)) disabled.push_back(found->id);
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

QString ExtensionManager::userExtensionDirectory()
{
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
    if (!shortcut.isEmpty()) registered->action->setShortcut(QKeySequence(shortcut));
    RegisteredCommand *raw = registered.get();
    connect(registered->action, &QAction::triggered, this, [this, raw] { triggerCommand(raw); });
    m_commands.push_back(std::move(registered));
    return true;
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

int32_t ExtensionManager::hostRegisterCommand(void *hostContext, NsExtensionHandle extension,
                                               const NsCommandSpec *command)
{
    auto *manager = static_cast<ExtensionManager *>(hostContext);
    auto *owner = static_cast<LoadedExtension *>(extension);
    if (!manager || manager->m_shuttingDown) return 0;
    const bool knownOwner = std::any_of(manager->m_extensions.cbegin(), manager->m_extensions.cend(),
        [owner](const auto &candidate) { return candidate.get() == owner; });
    return knownOwner && manager->registerCommand(owner, command) ? 1 : 0;
}

void ExtensionManager::hostShowMessage(void *hostContext, NsStringView message, uint32_t timeoutMs)
{
    auto *manager = static_cast<ExtensionManager *>(hostContext);
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
    auto *manager = static_cast<ExtensionManager *>(hostContext);
    if (!manager || !manager->m_window) return 0;
    const QString filePath = fromView(path);
    if (filePath.isEmpty()) return 0;
    manager->m_window->openFile(filePath);
    return 1;
}

int32_t ExtensionManager::hostInsertText(void *hostContext, NsStringView text)
{
    auto *manager = static_cast<ExtensionManager *>(hostContext);
    return manager && manager->m_window
        && manager->m_window->insertExtensionText(fromView(text)) ? 1 : 0;
}

int32_t ExtensionManager::hostReplaceDocumentText(void *hostContext, NsStringView text)
{
    auto *manager = static_cast<ExtensionManager *>(hostContext);
    return manager && manager->m_window
        && manager->m_window->replaceExtensionDocument(fromView(text)) ? 1 : 0;
}

int32_t ExtensionManager::hostRunProcess(void *hostContext, NsStringView program,
                                         const NsStringView *arguments, size_t argumentCount,
                                         NsStringView workingDirectory)
{
    auto *manager = static_cast<ExtensionManager *>(hostContext);
    if (!manager || !manager->m_window || argumentCount > 4096
        || (argumentCount > 0 && !arguments)) return 0;
    QStringList convertedArguments;
    convertedArguments.reserve(static_cast<qsizetype>(std::min(argumentCount,
        static_cast<size_t>(std::numeric_limits<qsizetype>::max()))));
    for (size_t index = 0; index < argumentCount; ++index)
        convertedArguments.push_back(fromView(arguments[index]));
    return manager->m_window->runExtensionProcess(fromView(program), convertedArguments,
                                                   fromView(workingDirectory)) ? 1 : 0;
}
