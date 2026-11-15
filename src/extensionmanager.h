#pragma once

#include "extension_api.h"

#include <QObject>
#include <QStringList>

#include <memory>
#include <functional>
#include <vector>

class QAction;
class QLibrary;
class QMenu;
class QWidget;
class MainWindow;

struct ExtensionViewInfo
{
    QString id;
    QString title;
    QString dockObjectName;
    uint32_t defaultLocation = NS_VIEW_LOCATION_LEFT;
};

struct NativeExtensionInfo
{
    QString id;
    QString name;
    QString version;
    QString description;
    QString author;
    QString filePath;
    QString manifestPath;
    QStringList permissions;
    uint32_t abiVersion = 0;
    bool bundled = false;
    bool enabled = true;
    bool loaded = false;
    bool pendingRestart = false;
};

struct ExtensionSettingsPageInfo
{
    QString title;
    QStringList keywords;
    QWidget *widget = nullptr;
    std::function<void()> apply;
    std::function<void()> restoreDefaults;
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
    [[nodiscard]] QStringList viewIds() const;
    [[nodiscard]] bool viewInfo(const QString &viewId, ExtensionViewInfo *result) const;
    QWidget *createView(const QString &viewId, QWidget *parent, const QString &projectRoot);
    void notifyProjectChanged(const QString &projectRoot);
    [[nodiscard]] QList<ExtensionSettingsPageInfo> createSettingsPages(QWidget *parent) const;
    void notifyDocumentWillSave();
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
    struct RegisteredEvent;
    struct RegisteredSettingsPage;
    struct RegisteredView;
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
    bool registerEvent(LoadedExtension *owner, uint32_t eventType,
                       NsEventCallback callback, void *userData);
    bool registerSettingsPage(LoadedExtension *owner, const NsSettingsPageSpec *page);
    bool registerView(LoadedExtension *owner, const NsViewSpec *view);
    void triggerCommand(RegisteredCommand *command);
    void removeCommands(LoadedExtension *owner);
    void removeEvents(LoadedExtension *owner);
    void removeSettingsPages(LoadedExtension *owner);
    void removeViews(LoadedExtension *owner);
    [[nodiscard]] QString extensionSetting(LoadedExtension *owner, const QString &key) const;
    [[nodiscard]] bool hasPermission(const LoadedExtension *owner, const QString &permission) const;
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
    static int32_t hostRegisterEventListener(void *hostContext, NsExtensionHandle extension,
                                             uint32_t eventType, NsEventCallback callback,
                                             void *userData);
    static size_t hostGetActiveDocumentText(void *hostContext, char *buffer, size_t capacity);
    static size_t hostGetActiveDocumentPath(void *hostContext, char *buffer, size_t capacity);
    static int32_t hostRegisterSettingsPage(void *hostContext, NsExtensionHandle extension,
                                            const NsSettingsPageSpec *page);
    static size_t hostGetExtensionSetting(void *hostContext, NsExtensionHandle extension,
                                          NsStringView key, char *buffer, size_t capacity);
    static int32_t hostShowView(void *hostContext, NsStringView viewId);
    static int32_t hostRegisterView(void *hostContext, NsExtensionHandle extension,
                                    const NsViewSpec *view);
    static int32_t hostSetFileDecorations(void *hostContext, NsExtensionHandle extension,
                                          const NsFileDecoration *decorations,
                                          size_t decorationCount);

    MainWindow *m_window = nullptr;
    QMenu *m_extensionsMenu = nullptr;
    NsHostApi m_hostApi{};
    std::vector<std::unique_ptr<LoadedExtension>> m_extensions;
    std::vector<std::unique_ptr<RegisteredCommand>> m_commands;
    std::vector<std::unique_ptr<RegisteredEvent>> m_events;
    std::vector<std::unique_ptr<RegisteredSettingsPage>> m_settingsPages;
    std::vector<std::unique_ptr<RegisteredView>> m_views;
    QList<PendingManifest> m_pendingManifests;
    int m_asyncLoadedCount = 0;
    bool m_asyncLoading = false;
    bool m_shuttingDown = false;
};
