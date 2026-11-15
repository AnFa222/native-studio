#pragma once

#include <QMainWindow>
#include <QStringList>

class CodeEditor;
class QAction;
class QIcon;
class QMenu;
class QLabel;
class LanguageManager;
class ExtensionManager;
class ProjectExplorer;
class QDockWidget;
class QTabWidget;
class QTreeWidget;
class TerminalWidget;
class QTreeView;

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

private:
    [[nodiscard]] CodeEditor *currentEditor() const;
    [[nodiscard]] CodeEditor *editorAt(int index) const;
    [[nodiscard]] int findEditor(const QString &path) const;
    CodeEditor *addEditor(const QString &title = tr("Untitled"));
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
    [[nodiscard]] static QIcon createRunIcon();

    QTabWidget *m_tabs = nullptr;
    ProjectExplorer *m_projectExplorer = nullptr;
    LanguageManager *m_languageManager = nullptr;
    ExtensionManager *m_extensionManager = nullptr;
    TerminalWidget *m_terminal = nullptr;
    QDockWidget *m_terminalDock = nullptr;
    QDockWidget *m_problemsDock = nullptr;
    QTreeWidget *m_problemsTree = nullptr;
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
};
