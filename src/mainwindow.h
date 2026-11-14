#pragma once

#include <QMainWindow>

class CodeEditor;
class QAction;
class QIcon;
class LanguageManager;
class ProjectExplorer;
class QDockWidget;
class QTabWidget;
class TerminalWidget;
class QTreeView;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    [[nodiscard]] QTabWidget *editorTabs() const;
    [[nodiscard]] QTreeView *projectTree() const;
    [[nodiscard]] ProjectExplorer *projectExplorer() const;
    [[nodiscard]] LanguageManager *languageManager() const;
    [[nodiscard]] TerminalWidget *terminal() const;

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
    void updateRunAction();
    [[nodiscard]] static QIcon createRunIcon();

    QTabWidget *m_tabs = nullptr;
    ProjectExplorer *m_projectExplorer = nullptr;
    LanguageManager *m_languageManager = nullptr;
    TerminalWidget *m_terminal = nullptr;
    QDockWidget *m_terminalDock = nullptr;
    QAction *m_runAction = nullptr;
    QString m_projectDirectory;
};
