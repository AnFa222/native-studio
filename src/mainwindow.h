#pragma once

#include <QMainWindow>
#include <QStringList>
#include <QVariant>

class CodeEditor;
class QAction;
class QDragEnterEvent;
class QDropEvent;
class QIcon;
class QMenu;
class QLabel;
class LanguageManager;
class ExtensionManager;
class ProjectExplorer;
class QDockWidget;
class QTabWidget;
class QTreeWidget;
class QSplitter;
class TerminalWidget;
class QTreeView;
class QToolButton;
class ProjectSearchWidget;
class GitWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    [[nodiscard]] QTabWidget *editorTabs() const;
    [[nodiscard]] QTreeView *projectTree() const;
    [[nodiscard]] ProjectExplorer *projectExplorer() const;
    [[nodiscard]] LanguageManager *languageManager() const;
    [[nodiscard]] ExtensionManager *extensionManager() const;
    [[nodiscard]] TerminalWidget *terminal() const;
    [[nodiscard]] QString activeFilePath() const;
    [[nodiscard]] QString projectDirectory() const;
    [[nodiscard]] bool activeDocumentModified() const;
    [[nodiscard]] QString activeDocumentText() const;
    void startDeferredServices();
    void loadServicesNow();
    void restoreSessionDeferred();
    bool restoreWindowLayout();
    void showExtensionMessage(const QString &message, int timeoutMs = 3000);
    bool insertExtensionText(const QString &text);
    bool replaceExtensionDocument(const QString &text);
    bool runExtensionProcess(const QString &program, const QStringList &arguments,
                             const QString &workingDirectory = {});

signals:
    void servicesLoaded();

public slots:
    void openProject(const QString &directory);
    void openFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void createFile();
    void chooseFile();
    void chooseProject();
    bool saveCurrentFile();
    bool saveCurrentFileAs();
    void closeTab(int index);
    void reloadLanguagePlugins();
    void managePlugins();
    void manageExtensions();
    void runCurrentFile();
    void quickOpen();
    void showProjectSearch();
    void splitEditorRight();
    void showKeybindings();
    void showGit();
    void newTerminal();
    void newPowerShellTerminal();

private:
    [[nodiscard]] CodeEditor *currentEditor() const;
    [[nodiscard]] CodeEditor *editorAt(int index) const;
    [[nodiscard]] int findEditor(const QString &path) const;
    [[nodiscard]] QPair<QTabWidget *, int> findEditorLocation(const QString &path) const;
    CodeEditor *addEditor(const QString &title = tr("Untitled"));
    CodeEditor *editorAt(QTabWidget *tabs, int index) const;
    QList<QTabWidget *> editorGroups() const;
    void configureEditorGroup(QTabWidget *tabs);
    void installTabCloseButton(QTabWidget *tabs, int index);
    void closeTabIn(QTabWidget *tabs, int index);
    bool saveEditor(CodeEditor *editor, bool forceDialog = false);
    bool confirmCloseEditor(CodeEditor *editor);
    void updateTabTitle(CodeEditor *editor);
    void createActions();
    void applyTheme();
    void applySettings();
    void updateRunAction();
    void updateLanguageFeatures(CodeEditor *editor);
    void refreshLanguageFeatures();
    void rebuildLanguageStatusWidgets();
    void updateLanguageStatusWidgets();
    void updateStartupStatus();
    void updateProblems(CodeEditor *editor);
    void updateOutline(CodeEditor *editor);
    void saveSession();
    void restoreSession();
    void restoreNextSessionDocument();
    void applyStoredKeybindings();
    TerminalWidget *createTerminal(const QString &title, const QString &program = {},
                                   const QStringList &arguments = {});
    void updateTerminalCloseButtons();
    void closeTerminalTab(int index);
    void connectTerminal(TerminalWidget *terminal);
    [[nodiscard]] static QIcon createRunIcon();

    QTabWidget *m_tabs = nullptr;
    QTabWidget *m_secondaryTabs = nullptr;
    QTabWidget *m_activeTabs = nullptr;
    QSplitter *m_editorSplitter = nullptr;
    ProjectExplorer *m_projectExplorer = nullptr;
    LanguageManager *m_languageManager = nullptr;
    ExtensionManager *m_extensionManager = nullptr;
    TerminalWidget *m_terminal = nullptr;
    QTabWidget *m_terminalTabs = nullptr;
    QToolButton *m_terminalTerminateButton = nullptr;
    QDockWidget *m_projectExplorerDock = nullptr;
    QDockWidget *m_terminalDock = nullptr;
    QDockWidget *m_problemsDock = nullptr;
    QTreeWidget *m_problemsTree = nullptr;
    QDockWidget *m_outlineDock = nullptr;
    QTreeWidget *m_outlineTree = nullptr;
    QDockWidget *m_searchDock = nullptr;
    ProjectSearchWidget *m_projectSearch = nullptr;
    QDockWidget *m_gitDock = nullptr;
    GitWidget *m_gitWidget = nullptr;
    QAction *m_runAction = nullptr;
    QMenu *m_extensionsMenu = nullptr;
    QLabel *m_cursorStatus = nullptr;
    struct LanguageStatusItem {
        QWidget *widget = nullptr;
        QStringList extensions;
    };
    QList<LanguageStatusItem> m_languageStatusItems;
    QString m_projectDirectory;
    bool m_servicesStarted = false;
    bool m_languageLoadingDone = false;
    bool m_extensionLoadingDone = false;
    QVariantList m_pendingSessionDocuments;
    int m_sessionDocumentIndex = 0;
    int m_sessionPrimaryIndex = 0;
    int m_sessionSecondaryIndex = -1;
    int m_sessionActiveGroup = 0;
    QByteArray m_sessionSplitterState;
};
