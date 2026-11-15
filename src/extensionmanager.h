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
    QString filePath;
    bool bundled = false;
};

class ExtensionManager final : public QObject
{
    Q_OBJECT

public:
    ExtensionManager(MainWindow *window, QMenu *extensionsMenu, QObject *parent = nullptr);
    ~ExtensionManager() override;

    int loadExtensions(const QString &directory, bool bundled = false);
    int loadDefaultExtensions();
    void shutdown();

    [[nodiscard]] QList<NativeExtensionInfo> loadedExtensions() const;
    [[nodiscard]] QStringList commandIds() const;
    [[nodiscard]] static QString userExtensionDirectory();

private:
    struct LoadedExtension;
    struct RegisteredCommand;

    [[nodiscard]] static QString fromView(NsStringView view);
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
    bool m_shuttingDown = false;
};
