#include "extensionmanager.h"

#include "mainwindow.h"

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QKeySequence>
#include <QLibrary>
#include <QMenu>
#include <QStandardPaths>

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

int ExtensionManager::loadExtensions(const QString &directory, bool bundled)
{
    if (m_shuttingDown) return 0;
    QDir extensionDirectory(directory);
    if (!extensionDirectory.exists()) return 0;

    int loadedCount = 0;
    const auto files = extensionDirectory.entryInfoList(QDir::Files | QDir::Readable,
                                                         QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files) {
        if (!QLibrary::isLibrary(file.fileName())) continue;
        auto extension = std::make_unique<LoadedExtension>();
        extension->library = std::make_unique<QLibrary>(file.absoluteFilePath());
        if (!extension->library->load()) {
            qWarning().noquote() << "Could not load native extension" << file.fileName()
                                 << extension->library->errorString();
            continue;
        }

        const auto entry = reinterpret_cast<NsExtensionEntryFunction>(
            extension->library->resolve(NS_EXTENSION_ENTRY_SYMBOL));
        if (!entry) {
            extension->library->unload();
            continue;
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
            continue;
        }

        const QString id = fromView(descriptor->id).trimmed();
        if (id.isEmpty() || containsExtension(id)) {
            extension->library->unload();
            continue;
        }
        extension->descriptor = descriptor;
        extension->info = {id, fromView(descriptor->name), fromView(descriptor->version),
                           file.absoluteFilePath(), bundled};
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
            continue;
        }
        ++loadedCount;
    }
    return loadedCount;
}

int ExtensionManager::loadDefaultExtensions()
{
    QDir().mkpath(userExtensionDirectory());
    int loaded = loadExtensions(userExtensionDirectory(), false);
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QString bundled = applicationDirectory.filePath(QStringLiteral("extensions"));
    loaded += loadExtensions(bundled, true);
    const QString multiConfig = QDir(applicationDirectory.filePath(QStringLiteral("..")))
                                    .filePath(QStringLiteral("extensions"));
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
