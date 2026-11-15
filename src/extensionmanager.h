#pragma once

#include "extension_api.h"

#include <QObject>
#include <QStringList>

#include <memory>
#include <vector>

class QAction;
class QLibrary;
class QMenu;
class MainWindow;

struct NativeExtensionInfo
{
    QString id;
    QString name;
    QString version;
    QString description;
    QString author;
    QString filePath;
    QString manifestPath;
    uint32_t abiVersion = 0;
    bool bundled = false;
    bool enabled = true;
    bool loaded = false;
    bool pendingRestart = false;
};

class ExtensionManager final : public QObject
{
    Q_OBJECT

public:
    ExtensionManager(MainWindow *window, QMenu *extensionsMenu, QObject *parent = nullptr);
    ~ExtensionManager() override;

    int loadExtensions(const QString &directory, bool bundled = false);
    int loadDefaultExtensions();
    void loadDefaultExtensionsAsync();
    void shutdown();

    [[nodiscard]] QList<NativeExtensionInfo> loadedExtensions() const;
    [[nodiscard]] QList<NativeExtensionInfo> installedExtensions() const;
    [[nodiscard]] QStringList commandIds() const;
    bool importExtension(const QString &manifestPath, QString *error = nullptr,
                         bool *restartRequired = nullptr);
    bool setExtensionEnabled(const QString &id, bool enabled, QString *error = nullptr);
    bool removeExtension(const QString &id, QString *error = nullptr);
    [[nodiscard]] static QString userExtensionDirectory();
    static void applyPendingOperations();

signals:
    void extensionLoaded(const QString &extensionId);
    void loadingFinished(int loadedCount);

private:
    struct LoadedExtension;
    struct RegisteredCommand;
    struct PendingManifest {
        QString filePath;
        bool bundled = false;
    };

    [[nodiscard]] static QString fromView(NsStringView view);
    [[nodiscard]] static bool readManifest(const QString &path, bool bundled,
                                           NativeExtensionInfo *info, QString *error = nullptr);
    [[nodiscard]] static QStringList disabledExtensionIds();
    static void setDisabledExtensionIds(const QStringList &ids);
    [[nodiscard]] static QString bundledExtensionDirectory();
    [[nodiscard]] static QString multiConfigExtensionDirectory();
    [[nodiscard]] static QString safeInstallBase(const QString &id);
    [[nodiscard]] QList<NativeExtensionInfo> manifestsInDirectory(const QString &directory,
                                                                  bool bundled) const;
    bool loadManifest(const QString &manifestPath, bool bundled);
    void queueManifestDirectory(const QString &directory, bool bundled);
    void loadNextManifest();
    [[nodiscard]] bool containsExtension(const QString &id) const;
    bool registerCommand(LoadedExtension *owner, const NsCommandSpec *command);
    void triggerCommand(RegisteredCommand *command);
    void removeCommands(LoadedExtension *owner);
    [[nodiscard]] QMenu *commandMenu(const QString &category);

    static int32_t hostRegisterCommand(void *hostContext, NsExtensionHandle extension,
                                       const NsCommandSpec *command);
    static void hostShowMessage(void *hostContext, NsStringView message, uint32_t timeoutMs);
    static void hostLog(void *hostContext, NsLogLevel level, NsStringView message);
    static int32_t hostOpenFile(void *hostContext, NsStringView path);
    static int32_t hostInsertText(void *hostContext, NsStringView text);
    static int32_t hostReplaceDocumentText(void *hostContext, NsStringView text);
    static int32_t hostRunProcess(void *hostContext, NsStringView program,
                                  const NsStringView *arguments, size_t argumentCount,
                                  NsStringView workingDirectory);

    MainWindow *m_window = nullptr;
    QMenu *m_extensionsMenu = nullptr;
    NsHostApi m_hostApi{};
    std::vector<std::unique_ptr<LoadedExtension>> m_extensions;
    std::vector<std::unique_ptr<RegisteredCommand>> m_commands;
    QList<PendingManifest> m_pendingManifests;
    int m_asyncLoadedCount = 0;
    bool m_asyncLoading = false;
    bool m_shuttingDown = false;
};
