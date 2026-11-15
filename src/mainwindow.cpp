#include "mainwindow.h"

#include "codeeditor.h"
#include "extensionmanager.h"
#include "extensionmanagerdialog.h"
#include "keybindingsdialog.h"
#include "languagemanager.h"
#include "pluginmanagerdialog.h"
#include "projectexplorer.h"
#include "projectsearchwidget.h"
#include "quickopendialog.h"
#include "settingsdialog.h"
#include "terminalwidget.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QSettings>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace {
constexpr auto editorTabMimeType = "application/x-native-studio-editor-tab";

class FileTabBar final : public QTabBar
{
public:
    using PathProvider = std::function<QString(int)>;
    using DropHandler = std::function<void(const QString &)>;
    void setPathProvider(PathProvider provider) { m_pathProvider = std::move(provider); }
    void setDropHandler(DropHandler handler)
    {
        m_dropHandler = std::move(handler);
        setAcceptDrops(true);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        m_pressPosition = event->position().toPoint();
        QTabBar::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->buttons() & Qt::LeftButton) && !rect().contains(event->position().toPoint())
            && (event->position().toPoint() - m_pressPosition).manhattanLength()
                >= QApplication::startDragDistance()) {
            const int index = tabAt(m_pressPosition);
            const QString path = m_pathProvider ? m_pathProvider(index) : QString{};
            if (!path.isEmpty()) {
                auto *mime = new QMimeData;
                mime->setUrls({QUrl::fromLocalFile(path)});
                mime->setData(editorTabMimeType, path.toUtf8());
                auto *drag = new QDrag(this);
                drag->setMimeData(mime);
                drag->exec(Qt::CopyAction);
                return;
            }
        }
        QTabBar::mouseMoveEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasFormat(editorTabMimeType)) event->acceptProposedAction();
        else QTabBar::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        if (event->mimeData()->hasFormat(editorTabMimeType) && m_dropHandler) {
            m_dropHandler(QString::fromUtf8(event->mimeData()->data(editorTabMimeType)));
            event->acceptProposedAction();
            return;
        }
        QTabBar::dropEvent(event);
    }

private:
    QPoint m_pressPosition;
    PathProvider m_pathProvider;
    DropHandler m_dropHandler;
};

class FileTabWidget final : public QTabWidget
{
public:
    explicit FileTabWidget(QWidget *parent = nullptr) : QTabWidget(parent)
    {
        setTabBar(new FileTabBar);
        setAcceptDrops(true);
        m_emptyState = new QWidget(this);
        m_emptyState->setObjectName(QStringLiteral("editorEmptyState"));
        auto *layout = new QVBoxLayout(m_emptyState);
        layout->setContentsMargins(24, 24, 24, 24);
        layout->setSpacing(10);
        auto *title = new QLabel(tr("No file is open"), m_emptyState);
        title->setObjectName(QStringLiteral("editorEmptyTitle"));
        auto *description = new QLabel(tr("Open a file or create a new one to start editing."),
                                       m_emptyState);
        description->setObjectName(QStringLiteral("editorEmptyDescription"));
        m_newFile = new QPushButton(tr("New File"), m_emptyState);
        m_openFile = new QPushButton(tr("Open File"), m_emptyState);
        m_openFolder = new QPushButton(tr("Open Folder"), m_emptyState);
        for (QPushButton *button : {m_newFile, m_openFile, m_openFolder}) {
            button->setFlat(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        }
        layout->addStretch();
        layout->addWidget(title, 0, Qt::AlignHCenter);
        layout->addWidget(description, 0, Qt::AlignHCenter);
        layout->addSpacing(6);
        layout->addWidget(m_newFile, 0, Qt::AlignHCenter);
        layout->addWidget(m_openFile, 0, Qt::AlignHCenter);
        layout->addWidget(m_openFolder, 0, Qt::AlignHCenter);
        layout->addStretch();
        updateEmptyState();
    }
    FileTabBar *fileTabBar() const { return static_cast<FileTabBar *>(tabBar()); }
    void setDropHandler(FileTabBar::DropHandler handler)
    {
        m_dropHandler = std::move(handler);
        fileTabBar()->setDropHandler(m_dropHandler);
    }
    void setEmptyActions(std::function<void()> newFile, std::function<void()> openFile,
                         std::function<void()> openFolder)
    {
        connect(m_newFile, &QPushButton::clicked, this, std::move(newFile));
        connect(m_openFile, &QPushButton::clicked, this, std::move(openFile));
        connect(m_openFolder, &QPushButton::clicked, this, std::move(openFolder));
    }

protected:
    void tabInserted(int index) override
    {
        QTabWidget::tabInserted(index);
        updateEmptyState();
    }

    void tabRemoved(int index) override
    {
        QTabWidget::tabRemoved(index);
        updateEmptyState();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QTabWidget::resizeEvent(event);
        m_emptyState->setGeometry(rect());
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasFormat(editorTabMimeType)) event->acceptProposedAction();
        else QTabWidget::dragEnterEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        if (event->mimeData()->hasFormat(editorTabMimeType) && m_dropHandler) {
            m_dropHandler(QString::fromUtf8(event->mimeData()->data(editorTabMimeType)));
            event->acceptProposedAction();
            return;
        }
        QTabWidget::dropEvent(event);
    }

private:
    void updateEmptyState()
    {
        const bool empty = count() == 0;
        tabBar()->setVisible(!empty);
        m_emptyState->setVisible(empty);
        if (empty) m_emptyState->raise();
    }

    FileTabBar::DropHandler m_dropHandler;
    QWidget *m_emptyState = nullptr;
    QPushButton *m_newFile = nullptr;
    QPushButton *m_openFile = nullptr;
    QPushButton *m_openFolder = nullptr;
};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabs(new FileTabWidget(this))
    , m_activeTabs(m_tabs)
    , m_editorSplitter(new QSplitter(Qt::Horizontal, this))
    , m_projectExplorer(new ProjectExplorer(this))
    , m_languageManager(new LanguageManager(this))
    , m_terminal(new TerminalWidget(this))
    , m_terminalTabs(new QTabWidget(this))
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("Native Studio"));
    resize(1200, 760);
    setAcceptDrops(true);

    m_tabs->setObjectName(QStringLiteral("editorTabs"));
    configureEditorGroup(m_tabs);
    m_editorSplitter->setObjectName(QStringLiteral("editorSplitter"));
    m_editorSplitter->addWidget(m_tabs);
    m_editorSplitter->installEventFilter(this);
    setCentralWidget(m_editorSplitter);

    connect(m_projectExplorer, &ProjectExplorer::fileActivated, this, &MainWindow::openFile);
    connect(m_projectExplorer, &ProjectExplorer::openFolderRequested, this, &MainWindow::chooseProject);

    m_projectExplorerDock = new QDockWidget(tr("EXPLORER"), this);
    m_projectExplorerDock->setObjectName(QStringLiteral("projectExplorerDock"));
    m_projectExplorerDock->toggleViewAction()->setObjectName(QStringLiteral("toggleExplorerAction"));
    m_projectExplorerDock->toggleViewAction()->setText(tr("Explorer"));
    m_projectExplorerDock->setWidget(m_projectExplorer);
    m_projectExplorerDock->setMinimumWidth(260);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectExplorerDock);

    m_terminalDock = new QDockWidget(tr("TERMINAL"), this);
    m_terminalDock->setObjectName(QStringLiteral("terminalDock"));
    m_terminalTabs->setObjectName(QStringLiteral("terminalTabs"));
    m_terminalTabs->setDocumentMode(true);
    m_terminalTabs->setTabsClosable(false);
    m_terminalTabs->addTab(m_terminal, tr("Terminal 1"));
    m_terminalTerminateButton = new QToolButton(m_terminalTabs);
    m_terminalTerminateButton->setObjectName(QStringLiteral("terminateTerminalButton"));
    m_terminalTerminateButton->setText({});
    m_terminalTerminateButton->setToolTip(tr("Terminate the running program or shell"));
    m_terminalTerminateButton->setAccessibleName(tr("Terminate"));
    m_terminalTerminateButton->setFocusPolicy(Qt::NoFocus);
    m_terminalTerminateButton->setCursor(Qt::PointingHandCursor);
    m_terminalTerminateButton->setFixedSize(12, 12);
    m_terminalTerminateButton->setEnabled(false);
    m_terminalTabs->setCornerWidget(m_terminalTerminateButton, Qt::TopRightCorner);
    connect(m_terminalTerminateButton, &QToolButton::clicked, this, [this] {
        if (m_terminal) m_terminal->terminateActiveProcess();
    });
    connectTerminal(m_terminal);
    connect(m_terminalTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (auto *terminal = qobject_cast<TerminalWidget *>(m_terminalTabs->widget(index)))
            m_terminal = terminal;
        m_terminalTerminateButton->setEnabled(m_terminal && m_terminal->canTerminate());
    });
    m_terminalDock->setWidget(m_terminalTabs);
    m_terminalDock->setMinimumHeight(160);
    addDockWidget(Qt::BottomDockWidgetArea, m_terminalDock);

    m_problemsTree = new QTreeWidget(this);
    m_problemsTree->setObjectName(QStringLiteral("problemsTree"));
    m_problemsTree->setColumnCount(3);
    m_problemsTree->setHeaderLabels({tr("Severity"), tr("Location"), tr("Message")});
    m_problemsTree->setRootIsDecorated(false);
    m_problemsTree->setAlternatingRowColors(true);
    m_problemsTree->header()->setStretchLastSection(true);
    m_problemsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_problemsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_problemsDock = new QDockWidget(tr("PROBLEMS"), this);
    m_problemsDock->setObjectName(QStringLiteral("problemsDock"));
    m_problemsDock->setWidget(m_problemsTree);
    m_problemsDock->setMinimumHeight(140);
    addDockWidget(Qt::BottomDockWidgetArea, m_problemsDock);
    tabifyDockWidget(m_terminalDock, m_problemsDock);
    m_problemsDock->hide();
    connect(m_problemsTree, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem *item) {
        if (!item || item->childCount() > 0) return;
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty()) openFile(path);
        auto *editor = currentEditor();
        if (!editor) return;
        const QTextBlock block = editor->document()->findBlockByNumber(
            item->data(1, Qt::UserRole).toInt());
        if (!block.isValid()) return;
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + item->data(2, Qt::UserRole).toInt());
        editor->setTextCursor(cursor);
        editor->setFocus();
    });

    m_outlineTree = new QTreeWidget(this);
    m_outlineTree->setObjectName(QStringLiteral("outlineTree"));
    m_outlineTree->setHeaderLabels({tr("Symbol"), tr("Kind")});
    m_outlineDock = new QDockWidget(tr("OUTLINE"), this);
    m_outlineDock->setObjectName(QStringLiteral("outlineDock"));
    m_outlineDock->setWidget(m_outlineTree);
    addDockWidget(Qt::RightDockWidgetArea, m_outlineDock);
    m_outlineDock->hide();
    connect(m_outlineTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        auto *editor = currentEditor();
        if (!editor || !item) return;
        const QTextBlock block = editor->document()->findBlockByNumber(
            item->data(0, Qt::UserRole).toInt());
        if (!block.isValid()) return;
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + item->data(1, Qt::UserRole).toInt());
        editor->setTextCursor(cursor);
        editor->setFocus();
    });

    createActions();
    createFindBar();
    applyStoredKeybindings();
    applyTheme();
    m_extensionManager = new ExtensionManager(this, m_extensionsMenu, this);
    m_cursorStatus = new QLabel(this);
    m_cursorStatus->setObjectName(QStringLiteral("cursorStatus"));
    statusBar()->addPermanentWidget(m_cursorStatus);
    connect(m_languageManager, &LanguageManager::pluginLoaded,
            this, [this] {
        refreshLanguageFeatures();
        rebuildLanguageStatusWidgets();
    });
    connect(m_languageManager, &LanguageManager::loadingFinished, this, [this](int) {
        m_languageLoadingDone = true;
        updateStartupStatus();
    });
    connect(m_extensionManager, &ExtensionManager::loadingFinished, this, [this](int) {
        m_extensionLoadingDone = true;
        restoreSavedExtensionViews();
        updateStartupStatus();
    });
    statusBar()->showMessage(tr("Ready — loading language support and extensions…"));
    addEditor();
}

MainWindow::~MainWindow()
{
    if (m_extensionManager) m_extensionManager->shutdown();
}

QTabWidget *MainWindow::editorTabs() const { return m_tabs; }
QTreeView *MainWindow::projectTree() const { return m_projectExplorer->treeView(); }
ProjectExplorer *MainWindow::projectExplorer() const { return m_projectExplorer; }
LanguageManager *MainWindow::languageManager() const { return m_languageManager; }
ExtensionManager *MainWindow::extensionManager() const { return m_extensionManager; }
TerminalWidget *MainWindow::terminal() const { return m_terminal; }
void MainWindow::restoreSessionDeferred() { QTimer::singleShot(0, this, &MainWindow::restoreSession); }
bool MainWindow::restoreWindowLayout()
{
    QSettings settings;
    if (!settings.contains(QStringLiteral("window/geometry"))) return false;
    const bool restored = restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    m_savedWindowState = settings.value(QStringLiteral("window/state")).toByteArray();
    m_windowLayoutRestored = restoreState(m_savedWindowState);
    return restored;
}

void MainWindow::startDeferredServices()
{
    if (m_servicesStarted) return;
    m_servicesStarted = true;
    m_terminal->startDeferred();
    m_languageManager->loadDefaultPluginsAsync();
    m_extensionManager->loadDefaultExtensionsAsync();
}

void MainWindow::loadServicesNow()
{
    if (m_servicesStarted) return;
    m_servicesStarted = true;
    m_terminal->startDeferred();
    m_languageManager->loadDefaultPlugins();
    m_extensionManager->loadDefaultExtensions();
    m_languageLoadingDone = true;
    m_extensionLoadingDone = true;
    restoreSavedExtensionViews();
    updateStartupStatus();
}

void MainWindow::restoreSavedExtensionViews()
{
    if (!m_extensionLoadingDone || !m_windowLayoutRestored) return;
    const QStringList visibleViews = QSettings().value(
        QStringLiteral("window/visibleExtensionViews")).toStringList();
    QList<QDockWidget *> restoredDocks;
    for (const QString &viewId : visibleViews) {
        if (!showExtensionView(viewId)) continue;
        for (QDockWidget *dock : findChildren<QDockWidget *>())
            if (dock->property("extensionViewId").toString() == viewId)
                restoredDocks.push_back(dock);
    }
    if (!restoredDocks.isEmpty()) restoreState(m_savedWindowState);
    for (QDockWidget *dock : restoredDocks) dock->show();
}

void MainWindow::updateStartupStatus()
{
    if (!m_languageLoadingDone || !m_extensionLoadingDone) return;
    statusBar()->showMessage(tr("Ready — %1 language plugin(s), %2 native extension(s) loaded")
                                 .arg(m_languageManager->languages().size())
                                 .arg(m_extensionManager->loadedExtensions().size()), 3000);
    emit servicesLoaded();
}

QString MainWindow::activeFilePath() const
{
    const auto *editor = currentEditor();
    return editor ? editor->filePath() : QString{};
}
QString MainWindow::projectDirectory() const { return m_projectDirectory; }
bool MainWindow::activeDocumentModified() const
{
    const auto *editor = currentEditor();
    return editor && editor->document()->isModified();
}
QString MainWindow::activeDocumentText() const
{
    const auto *editor = currentEditor();
    return editor ? editor->toPlainText() : QString{};
}
void MainWindow::showExtensionMessage(const QString &message, int timeoutMs)
{
    statusBar()->showMessage(message, qMax(0, timeoutMs));
}
bool MainWindow::insertExtensionText(const QString &text)
{
    auto *editor = currentEditor();
    if (!editor) return false;
    QTextCursor cursor = editor->textCursor();
    cursor.insertText(text);
    editor->setTextCursor(cursor);
    editor->setFocus();
    return true;
}
bool MainWindow::replaceExtensionDocument(const QString &text)
{
    auto *editor = currentEditor();
    if (!editor) return false;
    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    editor->setFocus();
    return true;
}
bool MainWindow::runExtensionProcess(const QString &program, const QStringList &arguments,
                                     const QString &workingDirectory)
{
    if (program.trimmed().isEmpty()) return false;
    m_terminalDock->show();
    m_terminalDock->raise();
    m_terminal->executeProgram(program, arguments,
        workingDirectory.isEmpty() ? m_projectDirectory : workingDirectory);
    return true;
}

bool MainWindow::showExtensionView(const QString &viewId)
{
    for (QDockWidget *dock : findChildren<QDockWidget *>()) {
        if (dock->property("extensionViewId").toString() != viewId) continue;
        dock->show();
        dock->raise();
        return true;
    }
    ExtensionViewInfo info;
    if (!m_extensionManager->viewInfo(viewId, &info)) return false;
    auto *dock = new QDockWidget(info.title, this);
    dock->setObjectName(info.dockObjectName);
    dock->setProperty("extensionViewId", viewId);
    QWidget *view = m_extensionManager->createView(viewId, dock, m_projectDirectory);
    if (!view) {
        delete dock;
        return false;
    }
    dock->setWidget(view);
    Qt::DockWidgetArea area = Qt::LeftDockWidgetArea;
    if (info.defaultLocation == NS_VIEW_LOCATION_RIGHT) area = Qt::RightDockWidgetArea;
    else if (info.defaultLocation == NS_VIEW_LOCATION_BOTTOM) area = Qt::BottomDockWidgetArea;
    if (!m_windowLayoutRestored || !restoreDockWidget(dock)) addDockWidget(area, dock);
    dock->show();
    dock->raise();
    return true;
}

void MainWindow::setExtensionFileDecorations(
    const QString &extensionId, const QList<ExplorerFileDecoration> &decorations)
{
    m_projectExplorer->setFileDecorations(extensionId, decorations);
}

void MainWindow::createActions()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    auto *projectMenu = menuBar()->addMenu(tr("&Project"));
    auto *terminalMenu = menuBar()->addMenu(tr("&Terminal"));
    auto *pluginsMenu = menuBar()->addMenu(tr("&Plugins"));
    m_extensionsMenu = menuBar()->addMenu(tr("&Extensions"));
    auto *runMenu = menuBar()->addMenu(tr("&Run"));
    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    auto *toolbar = addToolBar(tr("File"));
    toolbar->setObjectName(QStringLiteral("fileToolbar"));
    toolbar->setMovable(false);

    auto addAction = [fileMenu, toolbar](const QString &text, const QKeySequence &shortcut,
                                         const auto &receiver, auto slot) {
        auto *action = fileMenu->addAction(text, receiver, slot);
        action->setShortcut(shortcut);
        toolbar->addAction(action);
        return action;
    };
    auto *newAction = addAction(tr("&New"), QKeySequence::New, this, &MainWindow::createFile);
    newAction->setObjectName(QStringLiteral("newFileAction"));
    auto *openAction = addAction(tr("&Open File..."), QKeySequence::Open, this, &MainWindow::chooseFile);
    openAction->setObjectName(QStringLiteral("openFileAction"));
    fileMenu->addSeparator();
    auto *saveAction = addAction(tr("&Save"), QKeySequence::Save,
                                 this, &MainWindow::saveCurrentFile);
    saveAction->setObjectName(QStringLiteral("saveFileAction"));
    auto *saveAsAction = addAction(tr("Save &As..."), QKeySequence::SaveAs, this, &MainWindow::saveCurrentFileAs);
    saveAsAction->setObjectName(QStringLiteral("saveFileAsAction"));
    auto *toolbarSpacer = new QWidget(toolbar);
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(toolbarSpacer);
    m_runAction = toolbar->addAction(createRunIcon(), tr("Run Current File"),
                                     this, &MainWindow::runCurrentFile);
    m_runAction->setObjectName(QStringLiteral("runCurrentFileAction"));
    m_runAction->setToolTip(tr("Run Current File (Ctrl+F5)"));
    m_runAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F5));
    m_runAction->setVisible(false);
    fileMenu->addSeparator();
    auto *exitAction = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    exitAction->setShortcut(QKeySequence::Quit);

    auto *findAction = editMenu->addAction(tr("&Find…"), this, &MainWindow::showFind);
    findAction->setObjectName(QStringLiteral("findAction"));
    findAction->setShortcut(QKeySequence::Find);
    auto *replaceAction = editMenu->addAction(tr("Find and &Replace…"), this,
                                               &MainWindow::showReplace);
    replaceAction->setObjectName(QStringLiteral("replaceAction"));
    replaceAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    auto *findNextAction = editMenu->addAction(tr("Find Next"), this, &MainWindow::findNext);
    findNextAction->setObjectName(QStringLiteral("findNextAction"));
    findNextAction->setShortcut(QKeySequence::FindNext);
    auto *findPreviousAction = editMenu->addAction(tr("Find Previous"), this,
                                                   &MainWindow::findPrevious);
    findPreviousAction->setObjectName(QStringLiteral("findPreviousAction"));
    findPreviousAction->setShortcut(QKeySequence::FindPrevious);

    auto *openProjectAction = projectMenu->addAction(tr("&Open Folder..."), this, &MainWindow::chooseProject);
    openProjectAction->setObjectName(QStringLiteral("openProjectAction"));
    openProjectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    toolbar->insertAction(saveAction, openProjectAction);
    m_closeProjectAction = projectMenu->addAction(
        tr("Close Folder"), this, &MainWindow::closeProject);
    m_closeProjectAction->setObjectName(QStringLiteral("closeProjectAction"));
    m_closeProjectAction->setEnabled(false);
    toolbar->insertAction(saveAction, m_closeProjectAction);
    auto *quickOpenAction = projectMenu->addAction(tr("Quick Open…"), this, &MainWindow::quickOpen);
    quickOpenAction->setObjectName(QStringLiteral("quickOpenAction"));
    quickOpenAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    auto *searchAction = projectMenu->addAction(tr("Search in Files…"), this, &MainWindow::showProjectSearch);
    searchAction->setObjectName(QStringLiteral("projectSearchAction"));
    searchAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    pluginsMenu->addAction(tr("Plugin Explorer…"), this, &MainWindow::managePlugins);
    pluginsMenu->addAction(tr("Rescan Language Plugins"), this, &MainWindow::reloadLanguagePlugins);
    auto *extensionStoreAction = m_extensionsMenu->addAction(
        tr("Extension Store…"), this, &MainWindow::manageExtensions);
    extensionStoreAction->setObjectName(QStringLiteral("extensionStoreAction"));
    toolbar->insertAction(saveAction, extensionStoreAction);
    m_extensionsMenu->addSeparator();
    runMenu->addAction(m_runAction);
    viewMenu->addAction(m_projectExplorerDock->toggleViewAction());
    viewMenu->addAction(m_terminalDock->toggleViewAction());
    viewMenu->addAction(m_problemsDock->toggleViewAction());
    viewMenu->addAction(m_outlineDock->toggleViewAction());
    auto *splitAction = viewMenu->addAction(tr("Split Editor Right"), this, &MainWindow::splitEditorRight);
    splitAction->setObjectName(QStringLiteral("splitEditorAction"));
    splitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+\\")));
    auto *newTerminalAction = terminalMenu->addAction(tr("New Terminal"), this, &MainWindow::newTerminal);
    newTerminalAction->setObjectName(QStringLiteral("newTerminalAction"));
    newTerminalAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+`")));
#ifdef Q_OS_WIN
    auto *powerShellAction = terminalMenu->addAction(tr("New PowerShell Terminal"),
                                                     this, &MainWindow::newPowerShellTerminal);
    powerShellAction->setObjectName(QStringLiteral("newPowerShellTerminalAction"));
#endif
    auto *preferencesAction = settingsMenu->addAction(tr("Preferences…"), this, [this] {
        SettingsDialog dialog(m_languageManager, m_extensionManager, this);
        if (dialog.exec() == QDialog::Accepted) applySettings();
    });
    preferencesAction->setObjectName(QStringLiteral("preferencesAction"));
    auto *keybindingsAction = settingsMenu->addAction(tr("Keyboard Shortcuts…"),
                                                       this, &MainWindow::showKeybindings);
    keybindingsAction->setObjectName(QStringLiteral("keybindingsAction"));
    keybindingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+K, Ctrl+S")));
}

void MainWindow::createFindBar()
{
    m_findBar = new QFrame(m_editorSplitter);
    m_findBar->setObjectName(QStringLiteral("findReplaceBar"));
    m_findBar->setFixedWidth(430);
    m_findBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Maximum);
    auto *panelLayout = new QVBoxLayout(m_findBar);
    panelLayout->setContentsMargins(7, 7, 7, 7);
    panelLayout->setSpacing(5);

    auto *findRow = new QHBoxLayout;
    findRow->setContentsMargins(0, 0, 0, 0);
    findRow->setSpacing(4);
    auto *toggleReplace = new QToolButton(m_findBar);
    toggleReplace->setObjectName(QStringLiteral("toggleReplaceButton"));
    toggleReplace->setText(QStringLiteral("›"));
    toggleReplace->setToolTip(tr("Toggle Replace"));
    toggleReplace->setCheckable(true);
    toggleReplace->setAutoRaise(true);
    m_findText = new QLineEdit(m_findBar);
    m_findText->setObjectName(QStringLiteral("findText"));
    m_findText->setPlaceholderText(tr("Find"));
    m_findText->setClearButtonEnabled(true);
    m_findCaseSensitive = new QToolButton(m_findBar);
    m_findCaseSensitive->setObjectName(QStringLiteral("findCaseSensitive"));
    m_findCaseSensitive->setText(tr("Aa"));
    m_findCaseSensitive->setToolTip(tr("Match Case"));
    m_findCaseSensitive->setCheckable(true);
    m_findCaseSensitive->setAutoRaise(true);
    const auto makeActionButton = [this](QAction *action, const QString &text,
                                         const QString &tooltip) {
        auto *button = new QToolButton(m_findBar);
        action->setText(text);
        action->setToolTip(tooltip);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        return button;
    };
    auto *previous = new QAction(this);
    previous->setObjectName(QStringLiteral("findPreviousButton"));
    connect(previous, &QAction::triggered, this, &MainWindow::findPrevious);
    auto *next = new QAction(this);
    next->setObjectName(QStringLiteral("findNextButton"));
    connect(next, &QAction::triggered, this, &MainWindow::findNext);
    auto *close = new QAction(this);
    close->setObjectName(QStringLiteral("closeFindButton"));
    connect(close, &QAction::triggered, m_findBar, &QWidget::hide);
    findRow->addWidget(toggleReplace);
    findRow->addWidget(m_findText, 1);
    findRow->addWidget(m_findCaseSensitive);
    findRow->addWidget(makeActionButton(previous, QStringLiteral("↑"), tr("Previous Match")));
    findRow->addWidget(makeActionButton(next, QStringLiteral("↓"), tr("Next Match")));
    findRow->addWidget(makeActionButton(close, QStringLiteral("×"), tr("Close")));
    panelLayout->addLayout(findRow);

    m_replaceRow = new QWidget(m_findBar);
    auto *replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(25, 0, 0, 0);
    replaceLayout->setSpacing(4);
    m_replaceText = new QLineEdit(m_replaceRow);
    m_replaceText->setObjectName(QStringLiteral("replaceText"));
    m_replaceText->setPlaceholderText(tr("Replace"));
    m_replaceText->setClearButtonEnabled(true);
    auto *replace = new QAction(this);
    replace->setObjectName(QStringLiteral("replaceCurrentButton"));
    connect(replace, &QAction::triggered, this, &MainWindow::replaceCurrent);
    auto *replaceEvery = new QAction(this);
    replaceEvery->setObjectName(QStringLiteral("replaceAllButton"));
    connect(replaceEvery, &QAction::triggered, this, &MainWindow::replaceAll);
    replaceLayout->addWidget(m_replaceText, 1);
    replaceLayout->addWidget(makeActionButton(replace, QStringLiteral("↪"), tr("Replace")));
    replaceLayout->addWidget(makeActionButton(replaceEvery, QStringLiteral("⇉"), tr("Replace All")));
    panelLayout->addWidget(m_replaceRow);
    connect(toggleReplace, &QToolButton::toggled, this, [this, toggleReplace](bool visible) {
        toggleReplace->setText(visible ? QStringLiteral("⌄") : QStringLiteral("›"));
        setFindReplaceVisible(visible);
    });
    connect(m_findText, &QLineEdit::returnPressed, this, &MainWindow::findNext);
    connect(m_replaceText, &QLineEdit::returnPressed, this, &MainWindow::replaceCurrent);
    auto *escape = new QAction(m_findBar);
    escape->setShortcut(QKeySequence(Qt::Key_Escape));
    escape->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_findBar->addAction(escape);
    connect(escape, &QAction::triggered, m_findBar, &QWidget::hide);
    m_replaceRow->hide();
    m_findBar->hide();
}

void MainWindow::positionFindBar()
{
    if (!m_findBar || !m_editorSplitter) return;
    QTabWidget *tabs = m_activeTabs ? m_activeTabs : m_tabs;
    if (!tabs) return;
    const QPoint topRight = tabs->mapTo(m_editorSplitter, QPoint(tabs->width(), 0));
    const int x = qMax(8, topRight.x() - m_findBar->width() - 12);
    const int y = qMax(8, topRight.y() + (tabs->tabBar()->isVisible()
        ? tabs->tabBar()->height() + 6 : 12));
    m_findBar->adjustSize();
    m_findBar->move(x, y);
    m_findBar->raise();
}

void MainWindow::setFindReplaceVisible(bool visible)
{
    m_replaceRow->setVisible(visible);
    m_findBar->adjustSize();
    positionFindBar();
}

void MainWindow::showFind()
{
    if (!currentEditor()) return;
    if (auto *toggle = m_findBar->findChild<QToolButton *>(
            QStringLiteral("toggleReplaceButton"))) toggle->setChecked(false);
    m_findBar->show();
    setFindReplaceVisible(false);
    if (auto *editor = currentEditor(); editor && editor->textCursor().hasSelection())
        m_findText->setText(editor->textCursor().selectedText());
    positionFindBar();
    m_findText->selectAll();
    m_findText->setFocus();
}

void MainWindow::showReplace()
{
    showFind();
    if (!currentEditor()) return;
    if (auto *toggle = m_findBar->findChild<QToolButton *>(
            QStringLiteral("toggleReplaceButton"))) toggle->setChecked(true);
}

bool MainWindow::findText(bool backward)
{
    auto *editor = currentEditor();
    if (!editor || m_findText->text().isEmpty()) return false;
    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    if (m_findCaseSensitive->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    QTextCursor start = editor->textCursor();
    start.clearSelection();
    QTextCursor found = editor->document()->find(m_findText->text(), start, flags);
    if (found.isNull()) {
        QTextCursor wrap(editor->document());
        wrap.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        found = editor->document()->find(m_findText->text(), wrap, flags);
    }
    if (found.isNull()) {
        statusBar()->showMessage(tr("No matches for “%1”").arg(m_findText->text()), 2000);
        return false;
    }
    editor->setTextCursor(found);
    editor->ensureCursorVisible();
    return true;
}

void MainWindow::findNext() { findText(false); }
void MainWindow::findPrevious() { findText(true); }

void MainWindow::replaceCurrent()
{
    auto *editor = currentEditor();
    if (!editor) return;
    QTextCursor cursor = editor->textCursor();
    const Qt::CaseSensitivity sensitivity = m_findCaseSensitive->isChecked()
        ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (cursor.hasSelection()
        && cursor.selectedText().compare(m_findText->text(), sensitivity) == 0) {
        cursor.insertText(m_replaceText->text());
        editor->setTextCursor(cursor);
    }
    findNext();
}

void MainWindow::replaceAll()
{
    auto *editor = currentEditor();
    if (!editor || m_findText->text().isEmpty()) return;
    QTextDocument::FindFlags flags;
    if (m_findCaseSensitive->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    QTextCursor transaction(editor->document());
    transaction.beginEditBlock();
    QTextCursor search(editor->document());
    int replacements = 0;
    while (true) {
        QTextCursor match = editor->document()->find(m_findText->text(), search, flags);
        if (match.isNull()) break;
        match.insertText(m_replaceText->text());
        search.setPosition(match.position());
        ++replacements;
    }
    transaction.endEditBlock();
    statusBar()->showMessage(tr("Replaced %1 occurrence(s)").arg(replacements), 2500);
}

void MainWindow::applyTheme()
{
    QSettings settings;
    QFont applicationFont = QApplication::font();
    applicationFont.setPointSize(qBound(8, settings.value(QStringLiteral("appearance/uiFontSize"),
                                                           applicationFont.pointSize()).toInt(), 20));
    QApplication::setFont(applicationFont);
    const bool light = settings.value(QStringLiteral("appearance/theme"),
                                      QStringLiteral("dark")).toString() == QStringLiteral("light");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(light ? QStringLiteral("#ffffff")
                                                     : QStringLiteral("#0d1117")));
    palette.setColor(QPalette::WindowText, QColor(light ? QStringLiteral("#24292f")
                                                         : QStringLiteral("#c9d1d9")));
    palette.setColor(QPalette::Base, QColor(light ? QStringLiteral("#ffffff")
                                                   : QStringLiteral("#0d1117")));
    palette.setColor(QPalette::AlternateBase, QColor(light ? QStringLiteral("#f6f8fa")
                                                            : QStringLiteral("#161b22")));
    palette.setColor(QPalette::Text, QColor(light ? QStringLiteral("#24292f")
                                                   : QStringLiteral("#e6edf3")));
    palette.setColor(QPalette::Button, QColor(light ? QStringLiteral("#f6f8fa")
                                                     : QStringLiteral("#21262d")));
    palette.setColor(QPalette::ButtonText, QColor(light ? QStringLiteral("#24292f")
                                                         : QStringLiteral("#c9d1d9")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#0969da")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    QApplication::setPalette(palette);
    if (light) {
        setStyleSheet(QStringLiteral(R"(
            QMainWindow, QDialog { background: #ffffff; color: #24292f; }
            QMenuBar, QToolBar { background: #f6f8fa; color: #24292f; border-bottom: 1px solid #d0d7de; }
            QMenuBar::item:selected, QMenu::item:selected { background: #ddf4ff; color: #0969da; }
            QMenu { background: #ffffff; color: #24292f; border: 1px solid #d0d7de; padding: 4px; }
            QMenu::item { padding: 6px 28px 6px 12px; }
            QStatusBar { background: #0969da; color: #ffffff; }
            QStatusBar::item { border: 0; }
            QDockWidget { color: #24292f; }
            QDockWidget::title { background: #f6f8fa; color: #57606a; padding: 8px 10px; border-bottom: 1px solid #d0d7de; }
            QDockWidget#projectExplorerDock QWidget { background: #f6f8fa; }
            QToolButton { color: #24292f; background: transparent; border: 0; border-radius: 4px; }
            QToolButton:hover { background: #d0d7de; }
            QLabel#gitAccountStatus, QLabel#gitSectionLabel { color: #57606a; font-size: 10px; font-weight: 600; }
            QToolButton#editorTabCloseButton { color: #57606a; background: transparent; border: 0; border-radius: 3px; padding: 0; font-size: 15px; font-weight: 400; }
            QToolButton#editorTabCloseButton:hover { color: #24292f; background: #d0d7de; }
            QWidget#editorEmptyState { background: #ffffff; }
            QLabel#editorEmptyTitle { color: #24292f; font-size: 18px; font-weight: 600; }
            QLabel#editorEmptyDescription { color: #57606a; }
            QWidget#editorEmptyState QPushButton { color: #0969da; background: transparent; border: 0; padding: 3px 8px; }
            QWidget#editorEmptyState QPushButton:hover { color: #0550ae; text-decoration: underline; }
            QFrame#findReplaceBar { background: #ffffff; border: 1px solid #d0d7de; border-radius: 5px; }
            QFrame#findReplaceBar QLineEdit { background: #ffffff; }
            QFrame#findReplaceBar QToolButton { min-width: 22px; min-height: 22px; }
            QDockWidget#terminalDock QToolButton#terminateTerminalButton { background: #cf222e; border: 0; border-radius: 3px; padding: 0; }
            QDockWidget#terminalDock QToolButton#terminateTerminalButton:hover { background: #a40e26; }
            QDockWidget#terminalDock QToolButton#terminateTerminalButton:disabled { background: #d8a4aa; }
            QPlainTextEdit#codeEditor { background: #ffffff; color: #24292f; border: 0; selection-background-color: #b6e3ff; selection-color: #24292f; padding: 4px 0 0 6px; }
            QLineEdit, QComboBox, QSpinBox, QListWidget { background: #ffffff; color: #24292f; border: 1px solid #d0d7de; border-radius: 4px; padding: 5px 7px; }
            QTableWidget, QTreeWidget { background: #ffffff; alternate-background-color: #f6f8fa; color: #24292f; border: 1px solid #d0d7de; selection-background-color: #0969da; selection-color: #ffffff; }
            QPushButton { background: #f6f8fa; color: #24292f; border: 1px solid #d0d7de; border-radius: 4px; padding: 6px 12px; }
            QPushButton:hover { background: #d0d7de; }
            QLabel[settingsHeading="true"] { color: #0969da; font-size: 16px; font-weight: 700; }
            QHeaderView::section { background: #f6f8fa; color: #57606a; border: 0; border-right: 1px solid #d0d7de; border-bottom: 1px solid #d0d7de; padding: 7px; }
            QToolTip { background: #ffffff; color: #24292f; border: 1px solid #d0d7de; }
        )"));
        return;
    }
    setStyleSheet(QStringLiteral(R"(
        QMainWindow { background: #0d1117; color: #c9d1d9; }
        QMenuBar { background: #161b22; color: #c9d1d9; border-bottom: 1px solid #30363d; }
        QMenuBar::item { background: transparent; }
        QMenuBar::item:selected { background: #30363d; color: #ffffff; }
        QMenu { background: #21262d; color: #c9d1d9; border: 1px solid #484f58; padding: 4px; }
        QMenu::item { padding: 6px 28px 6px 12px; border-radius: 3px; }
        QMenu::item:selected { background: #1f6feb; color: #ffffff; }
        QMenu::separator { height: 1px; background: #30363d; margin: 4px 8px; }
        QToolBar { background: #161b22; color: #c9d1d9; border: 0; border-bottom: 1px solid #30363d; }
        QStatusBar { background: #0969da; color: #ffffff; border: 0; }
        QStatusBar::item { border: 0; }
        QDockWidget { color: #c9d1d9; font-weight: 600; }
        QDockWidget::title { background: #21262d; color: #8b949e; padding: 8px 10px; font-size: 11px; border-bottom: 1px solid #30363d; }
        QDockWidget#projectExplorerDock QWidget { background: #161b22; }
        QDockWidget#terminalDock QWidget { background: #010409; }
        QLabel#workspaceLabel { color: #58a6ff; font-size: 11px; font-weight: 700; }
        QToolButton { color: #c9d1d9; background: transparent; border: 0; border-radius: 4px; }
        QToolButton:hover { background: #30363d; color: #ffffff; }
        QToolButton:pressed { background: #1f6feb; }
        QLabel#gitAccountStatus, QLabel#gitSectionLabel { color: #8b949e; font-size: 10px; font-weight: 600; }
        QToolButton#editorTabCloseButton { color: #8b949e; background: transparent; border: 0; border-radius: 3px; padding: 0; font-size: 15px; font-weight: 400; }
        QToolButton#editorTabCloseButton:hover { color: #ffffff; background: #30363d; }
        QToolButton#editorTabCloseButton:pressed { color: #ffffff; background: #484f58; }
        QWidget#editorEmptyState { background: #0d1117; }
        QLabel#editorEmptyTitle { color: #e6edf3; font-size: 18px; font-weight: 600; }
        QLabel#editorEmptyDescription { color: #8b949e; }
        QWidget#editorEmptyState QPushButton { color: #58a6ff; background: transparent; border: 0; padding: 3px 8px; }
        QWidget#editorEmptyState QPushButton:hover { color: #79c0ff; text-decoration: underline; }
        QFrame#findReplaceBar { background: #161b22; border: 1px solid #30363d; border-radius: 5px; }
        QFrame#findReplaceBar QLineEdit { background: #0d1117; }
        QFrame#findReplaceBar QToolButton { min-width: 22px; min-height: 22px; }
        QDockWidget#terminalDock QToolButton#terminateTerminalButton { background: #da3633; border: 0; border-radius: 3px; padding: 0; }
        QDockWidget#terminalDock QToolButton#terminateTerminalButton:hover { background: #f85149; }
        QDockWidget#terminalDock QToolButton#terminateTerminalButton:pressed { background: #b62324; }
        QDockWidget#terminalDock QToolButton#terminateTerminalButton:disabled { background: #672626; }
        QPlainTextEdit#codeEditor { background: #0d1117; color: #e6edf3; border: 0; selection-background-color: #264f78; selection-color: #ffffff; padding: 4px 0 0 6px; }
        QPlainTextEdit#terminalOutput { background: #010409; color: #ffffff; border: 0; selection-background-color: #1f6feb; selection-color: #ffffff; padding: 5px 8px; }
        QLineEdit { background: #0d1117; color: #e6edf3; border: 1px solid #30363d; border-radius: 4px; padding: 5px 7px; selection-background-color: #1f6feb; }
        QLineEdit:focus { border: 1px solid #58a6ff; }
        QLineEdit#terminalInput { background: #010409; color: #79c0ff; border-color: #21262d; }
        QDialog#pluginExplorer { background: #0d1117; color: #c9d1d9; }
        QDialog#settingsDialog { background: #0d1117; color: #c9d1d9; }
        QLabel#settingsHeading, QLabel[settingsHeading="true"] { color: #58a6ff; font-size: 16px; font-weight: 700; }
        QLabel#pluginExplorerHeading { color: #58a6ff; font-size: 16px; font-weight: 700; }
        QLabel#pluginExplorerDescription { color: #8b949e; }
        QLabel#pluginExplorerStatus { color: #8b949e; padding: 2px; }
        QTableWidget#pluginTable { background: #0d1117; alternate-background-color: #161b22; color: #c9d1d9; border: 1px solid #30363d; gridline-color: #21262d; selection-background-color: #1f6feb; selection-color: #ffffff; }
        QTableWidget#pluginTable::item { padding: 6px; }
        QTreeWidget#problemsTree { background: #0d1117; alternate-background-color: #161b22; color: #c9d1d9; border: 0; outline: 0; selection-background-color: #1f6feb; selection-color: #ffffff; }
        QTreeWidget#problemsTree::item { padding: 5px; }
        QAbstractItemView#completionPopup { background: #161b22; color: #c9d1d9; border: 1px solid #484f58; selection-background-color: #1f6feb; selection-color: #ffffff; }
        QToolTip { background: #21262d; color: #e6edf3; border: 1px solid #484f58; padding: 5px; }
        QHeaderView::section { background: #21262d; color: #8b949e; border: 0; border-right: 1px solid #30363d; border-bottom: 1px solid #30363d; padding: 7px; font-weight: 600; }
        QPushButton { background: #21262d; color: #c9d1d9; border: 1px solid #30363d; border-radius: 4px; padding: 6px 12px; }
        QPushButton:hover { background: #30363d; color: #ffffff; }
        QPushButton:disabled { color: #6e7681; background: #161b22; }
        QPushButton[accent="true"] { background: #238636; color: #ffffff; border-color: #2ea043; font-weight: 600; }
        QPushButton[accent="true"]:hover { background: #2ea043; }
        QPushButton[accent="true"]:disabled { background: #1b4724; color: #8b949e; border-color: #245c2f; }
        QTreeView { background: #161b22; color: #c9d1d9; border: 0; outline: 0; selection-background-color: #1f6feb; selection-color: #ffffff; }
        QTreeView::item { height: 22px; border: 0; }
        QTreeView::item:hover:!selected { background: #21262d; color: #ffffff; }
        QTreeView::item:selected { background: #1f6feb; color: #ffffff; }
        QTabWidget::pane { background: #0d1117; border: 0; }
        QTabBar { background: #161b22; }
        QTabBar::tab { background: #161b22; color: #8b949e; padding: 5px 10px; border-right: 1px solid #30363d; border-bottom: 1px solid #30363d; }
        QTabBar::tab:hover { background: #21262d; color: #e6edf3; }
        QTabBar::tab:selected { background: #0d1117; color: #ffffff; border-top: 2px solid #58a6ff; border-bottom: 0; }
        QScrollBar:vertical { background: #0d1117; width: 12px; margin: 0; }
        QScrollBar::handle:vertical { background: #30363d; min-height: 28px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #484f58; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal { background: #0d1117; height: 12px; margin: 0; }
        QScrollBar::handle:horizontal { background: #30363d; min-width: 28px; border-radius: 5px; margin: 2px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
    )"));
}

void MainWindow::applySettings()
{
    applyTheme();
    m_terminal->reloadSettings();
    for (QTabWidget *tabs : editorGroups()) {
        for (int index = 0; index < tabs->count(); ++index) {
            if (auto *editor = editorAt(tabs, index)) {
                editor->reloadSettings();
                if (!editor->filePath().isEmpty()) {
                    editor->setSyntaxHighlighter(
                        m_languageManager->createHighlighter(editor->filePath(), editor->document()));
                }
            }
        }
    }
}

QList<QTabWidget *> MainWindow::editorGroups() const
{
    QList<QTabWidget *> groups{m_tabs};
    if (m_secondaryTabs) groups.push_back(m_secondaryTabs);
    return groups;
}

void MainWindow::configureEditorGroup(QTabWidget *tabs)
{
    tabs->setTabsClosable(false);
    tabs->setMovable(true);
    tabs->setDocumentMode(true);
    if (auto *fileTabs = dynamic_cast<FileTabWidget *>(tabs)) {
        fileTabs->fileTabBar()->setPathProvider([this, tabs](int index) {
            const auto *editor = editorAt(tabs, index);
            return editor ? editor->filePath() : QString{};
        });
        fileTabs->setDropHandler([this, tabs](const QString &path) {
            moveEditorToGroup(path, tabs);
        });
        fileTabs->setEmptyActions(
            [this, tabs] { m_activeTabs = tabs; createFile(); },
            [this, tabs] { m_activeTabs = tabs; chooseFile(); },
            [this, tabs] { m_activeTabs = tabs; chooseProject(); });
    }
    connect(tabs, &QTabWidget::currentChanged, this, [this, tabs] {
        if (tabs->currentIndex() >= 0) m_activeTabs = tabs;
        updateRunAction();
        updateProblems(currentEditor());
        updateOutline(currentEditor());
        updateLanguageStatusWidgets();
        if (m_findBar && m_findBar->isVisible()) positionFindBar();
    });
    connect(tabs->tabBar(), &QTabBar::tabBarClicked, this, [this, tabs] { m_activeTabs = tabs; });
    connect(tabs, &QTabWidget::tabCloseRequested, this,
            [this, tabs](int index) { closeTabIn(tabs, index); });
}

CodeEditor *MainWindow::editorAt(QTabWidget *tabs, int index) const
{
    return tabs ? qobject_cast<CodeEditor *>(tabs->widget(index)) : nullptr;
}

void MainWindow::installTabCloseButton(QTabWidget *tabs, int index)
{
    auto *closeButton = new QToolButton(tabs->tabBar());
    closeButton->setObjectName(QStringLiteral("editorTabCloseButton"));
    closeButton->setText(QStringLiteral("\u00d7"));
    closeButton->setToolTip(tr("Close tab"));
    closeButton->setAccessibleName(tr("Close tab"));
    closeButton->setAutoRaise(true);
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setFixedSize(18, 18);
    tabs->tabBar()->setTabButton(index, QTabBar::RightSide, closeButton);
    connect(closeButton, &QToolButton::clicked, this, [this, tabs, closeButton] {
        for (int tab = 0; tab < tabs->count(); ++tab) {
            if (tabs->tabBar()->tabButton(tab, QTabBar::RightSide) == closeButton) {
                closeTabIn(tabs, tab);
                return;
            }
        }
    });
}

CodeEditor *MainWindow::addEditor(const QString &title)
{
    QTabWidget *tabs = m_activeTabs ? m_activeTabs : m_tabs;
    auto *editor = new CodeEditor(tabs);
    editor->setAcceptDrops(false);
    editor->setIndentationDeltaProvider([this, editor](const QString &lineText) {
        return m_languageManager->indentationDeltaAfterLine(editor->filePath(), lineText);
    });
    const int index = tabs->addTab(editor, title);
    installTabCloseButton(tabs, index);
    tabs->setCurrentIndex(index);
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor] { updateTabTitle(editor); });
    connect(editor, &CodeEditor::cursorPositionDescriptionChanged,
            m_cursorStatus, &QLabel::setText);
    auto *analysisTimer = new QTimer(editor);
    analysisTimer->setSingleShot(true);
    analysisTimer->setInterval(220);
    connect(editor, &QPlainTextEdit::textChanged, analysisTimer,
            [analysisTimer] { analysisTimer->start(); });
    connect(analysisTimer, &QTimer::timeout, this,
            [this, editor] { updateLanguageFeatures(editor); });
    auto *autoSaveTimer = new QTimer(editor);
    autoSaveTimer->setObjectName(QStringLiteral("autoSaveTimer"));
    autoSaveTimer->setSingleShot(true);
    connect(editor, &QPlainTextEdit::textChanged, autoSaveTimer, [editor, autoSaveTimer] {
        QSettings settings;
        if (!settings.value(QStringLiteral("editor/autoSave"), false).toBool()
            || editor->filePath().isEmpty()) {
            autoSaveTimer->stop();
            return;
        }
        autoSaveTimer->start(qBound(250,
            settings.value(QStringLiteral("editor/autoSaveDelayMs"), 1000).toInt(), 10000));
    });
    connect(autoSaveTimer, &QTimer::timeout, this, [this, editor] {
        if (editor == currentEditor() && editor->document()->isModified()
            && !editor->filePath().isEmpty()) saveEditor(editor);
    });
    editor->setFocus();
    updateRunAction();
    return editor;
}

void MainWindow::createFile() { addEditor(); }

void MainWindow::chooseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open File"), m_projectDirectory, tr("All Files (*)"));
    if (!path.isEmpty()) openFile(path);
}

void MainWindow::chooseProject()
{
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Open Project"), m_projectDirectory);
    if (!directory.isEmpty()) openProject(directory);
}

void MainWindow::closeProject()
{
    if (m_projectDirectory.isEmpty()) return;
    for (QTabWidget *tabs : editorGroups())
        for (int index = 0; index < tabs->count(); ++index)
            if (!confirmCloseEditor(editorAt(tabs, index))) return;

    for (QTabWidget *tabs : editorGroups()) {
        while (tabs->count() > 0) {
            QWidget *editor = tabs->widget(0);
            tabs->removeTab(0);
            editor->deleteLater();
        }
    }
    if (m_secondaryTabs) {
        m_secondaryTabs->deleteLater();
        m_secondaryTabs = nullptr;
    }
    m_activeTabs = m_tabs;
    m_projectDirectory.clear();
    m_projectExplorer->clearRootPath();
    if (m_projectSearch) m_projectSearch->setRootPath({});
    if (m_extensionManager) m_extensionManager->notifyProjectChanged({});
    if (m_closeProjectAction) m_closeProjectAction->setEnabled(false);
    if (m_findBar) m_findBar->hide();
    setWindowTitle(tr("Native Studio"));
    updateRunAction();
    updateProblems(nullptr);
    updateOutline(nullptr);
    if (m_cursorStatus) m_cursorStatus->clear();
    updateLanguageStatusWidgets();
    statusBar()->showMessage(tr("Folder closed"), 2500);
}

void MainWindow::openProject(const QString &directory)
{
    const QFileInfo info(directory);
    if (!info.isDir()) return;
    m_projectDirectory = info.absoluteFilePath();
    m_projectExplorer->setRootPath(m_projectDirectory);
    for (int index = 0; index < m_terminalTabs->count(); ++index)
        if (auto *terminal = qobject_cast<TerminalWidget *>(m_terminalTabs->widget(index)))
            terminal->setWorkingDirectory(m_projectDirectory);
    if (m_projectSearch) m_projectSearch->setRootPath(m_projectDirectory);
    m_extensionManager->notifyProjectChanged(m_projectDirectory);
    if (m_closeProjectAction) m_closeProjectAction->setEnabled(true);
    setWindowTitle(tr("%1 — Native Studio").arg(info.fileName()));
    statusBar()->showMessage(tr("Opened project %1").arg(QDir::toNativeSeparators(m_projectDirectory)), 4000);
}

int MainWindow::findEditor(const QString &path) const
{
    const auto location = findEditorLocation(path);
    return location.first == m_tabs ? location.second : -1;
}

QPair<QTabWidget *, int> MainWindow::findEditorLocation(const QString &path) const
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    for (QTabWidget *tabs : editorGroups()) {
    for (int index = 0; index < tabs->count(); ++index) {
        const auto *editor = editorAt(tabs, index);
        if (editor && QFileInfo(editor->filePath()).canonicalFilePath() == canonical)
            return {tabs, index};
    }
    }
    return {nullptr, -1};
}

void MainWindow::openFile(const QString &path)
{
    const auto existing = findEditorLocation(path);
    if (existing.first) {
        m_activeTabs = existing.first;
        existing.first->setCurrentIndex(existing.second);
        return;
    }

    CodeEditor *editor = nullptr;
    QTabWidget *tabs = m_activeTabs ? m_activeTabs : m_tabs;
    if (tabs->count() == 1 && editorAt(tabs, 0)->filePath().isEmpty()
        && !editorAt(tabs, 0)->document()->isModified() && editorAt(tabs, 0)->document()->isEmpty()) {
        editor = editorAt(tabs, 0);
    } else {
        editor = addEditor();
    }

    QString error;
    if (!editor->loadFromFile(path, &error)) {
        QMessageBox::critical(this, tr("Could Not Open File"), tr("%1\n\n%2").arg(path, error));
        if (editor->filePath().isEmpty() && editor->document()->isEmpty() && tabs->count() > 1) {
            tabs->removeTab(tabs->indexOf(editor));
            editor->deleteLater();
        }
        return;
    }
    editor->setSyntaxHighlighter(m_languageManager->createHighlighter(path, editor->document()));
    updateLanguageFeatures(editor);
    updateTabTitle(editor);
    updateRunAction();
    updateLanguageStatusWidgets();
    statusBar()->showMessage(tr("%1 — %2")
                                 .arg(QDir::toNativeSeparators(path),
                                      m_languageManager->languageForFile(path)), 3000);
}

CodeEditor *MainWindow::currentEditor() const
{
    QTabWidget *tabs = m_activeTabs ? m_activeTabs : m_tabs;
    return editorAt(tabs, tabs->currentIndex());
}
CodeEditor *MainWindow::editorAt(int index) const { return qobject_cast<CodeEditor *>(m_tabs->widget(index)); }

void MainWindow::updateTabTitle(CodeEditor *editor)
{
    QTabWidget *owner = nullptr;
    int index = -1;
    for (QTabWidget *tabs : editorGroups()) {
        index = tabs->indexOf(editor);
        if (index >= 0) { owner = tabs; break; }
    }
    if (!owner) return;
    QString title = editor->filePath().isEmpty() ? tr("Untitled") : QFileInfo(editor->filePath()).fileName();
    if (editor->document()->isModified()) title += QLatin1Char('*');
    owner->setTabText(index, title);
    owner->setTabToolTip(index, editor->filePath());
}

bool MainWindow::saveEditor(CodeEditor *editor, bool forceDialog)
{
    if (!editor) return false;
    QString path = editor->filePath();
    if (forceDialog || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save File"),
            path.isEmpty() ? m_projectDirectory + QStringLiteral("/untitled.txt") : path,
            tr("All Files (*)"));
        if (path.isEmpty()) return false;
    }
    for (QTabWidget *tabs : editorGroups()) {
        if (tabs->indexOf(editor) >= 0) {
            m_activeTabs = tabs;
            tabs->setCurrentWidget(editor);
            break;
        }
    }
    if (m_extensionManager) m_extensionManager->notifyDocumentWillSave();
    QString error;
    if (!editor->saveToFile(path, &error)) {
        QMessageBox::critical(this, tr("Could Not Save File"), tr("%1\n\n%2").arg(path, error));
        return false;
    }
    editor->setSyntaxHighlighter(m_languageManager->createHighlighter(path, editor->document()));
    updateLanguageFeatures(editor);
    updateTabTitle(editor);
    updateRunAction();
    updateLanguageStatusWidgets();
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)), 3000);
    return true;
}

bool MainWindow::saveCurrentFile() { return saveEditor(currentEditor()); }
bool MainWindow::saveCurrentFileAs() { return saveEditor(currentEditor(), true); }

void MainWindow::reloadLanguagePlugins()
{
    const int loaded = m_languageManager->loadDefaultPlugins();
    for (QTabWidget *tabs : editorGroups())
        for (int index = 0; index < tabs->count(); ++index)
            updateLanguageFeatures(editorAt(tabs, index));
    statusBar()->showMessage(tr("Loaded %1 new plugin(s); %2 language(s) available")
                                 .arg(loaded).arg(m_languageManager->languages().size()), 4000);
}

void MainWindow::refreshLanguageFeatures()
{
    for (QTabWidget *tabs : editorGroups()) {
    for (int index = 0; index < tabs->count(); ++index) {
        auto *editor = editorAt(tabs, index);
        if (!editor) continue;
        if (!editor->filePath().isEmpty()) {
            editor->setSyntaxHighlighter(
                m_languageManager->createHighlighter(editor->filePath(), editor->document()));
        }
        updateLanguageFeatures(editor);
    }
    }
    updateRunAction();
    updateLanguageStatusWidgets();
}

void MainWindow::rebuildLanguageStatusWidgets()
{
    for (const auto &item : m_languageStatusItems) {
        statusBar()->removeWidget(item.widget);
        item.widget->deleteLater();
    }
    m_languageStatusItems.clear();
    const auto widgets = m_languageManager->createStatusWidgets(statusBar());
    for (const auto &item : widgets) {
        statusBar()->insertWidget(0, item.widget);
        m_languageStatusItems.push_back({item.widget, item.extensions});
    }
    updateLanguageStatusWidgets();
}

void MainWindow::updateLanguageStatusWidgets()
{
    const auto *editor = currentEditor();
    const QString extension = editor
        ? QFileInfo(editor->filePath()).suffix().toLower() : QString{};
    for (const auto &item : m_languageStatusItems)
        item.widget->setVisible(!extension.isEmpty()
            && item.extensions.contains(extension, Qt::CaseInsensitive));
}

void MainWindow::updateLanguageFeatures(CodeEditor *editor)
{
    if (!editor) return;
    if (editor->filePath().isEmpty()) {
        editor->setCompletionItems({});
        editor->setDiagnostics({});
    } else {
        const QString source = editor->toPlainText();
        editor->setCompletionItems(
            m_languageManager->completionItems(editor->filePath(), source));
        editor->setDiagnostics(
            m_languageManager->diagnostics(editor->filePath(), source));
    }
    updateProblems(currentEditor());
    if (editor == currentEditor()) updateOutline(editor);
}

void MainWindow::updateProblems(CodeEditor *)
{
    if (!m_problemsTree || !m_problemsDock) return;
    m_problemsTree->clear();
    int errors = 0;
    int warnings = 0;
    for (QTabWidget *tabs : editorGroups()) {
    for (int tab = 0; tab < tabs->count(); ++tab) {
        auto *editor = editorAt(tabs, tab);
        if (!editor || editor->diagnostics().isEmpty()) continue;
        const QString path = editor->filePath();
        auto *fileItem = new QTreeWidgetItem(m_problemsTree);
        fileItem->setText(0, QFileInfo(path).fileName().isEmpty() ? tr("Untitled")
                                                                  : QFileInfo(path).fileName());
        fileItem->setFirstColumnSpanned(true);
        QTreeWidgetItem *errorGroup = nullptr;
        QTreeWidgetItem *warningGroup = nullptr;
        for (const auto &problem : editor->diagnostics()) {
            const bool isError = problem.severity == DiagnosticSeverity::Error;
            isError ? ++errors : ++warnings;
            QTreeWidgetItem *&severityGroup = isError ? errorGroup : warningGroup;
            if (!severityGroup) {
                severityGroup = new QTreeWidgetItem(fileItem);
                severityGroup->setText(0, isError ? tr("Errors") : tr("Warnings"));
                severityGroup->setFirstColumnSpanned(true);
            }
            auto *item = new QTreeWidgetItem(severityGroup);
            item->setText(0, isError ? tr("Error") : tr("Warning"));
            item->setText(1, tr("%1:%2").arg(problem.line + 1).arg(problem.column + 1));
            item->setText(2, problem.message);
            item->setData(0, Qt::UserRole, path);
            item->setData(1, Qt::UserRole, problem.line);
            item->setData(2, Qt::UserRole, problem.column);
            item->setForeground(0, isError ? QColor(QStringLiteral("#f85149"))
                                           : QColor(QStringLiteral("#d29922")));
        }
        fileItem->setExpanded(true);
        if (errorGroup) errorGroup->setExpanded(true);
        if (warningGroup) warningGroup->setExpanded(true);
    }
    }
    const int total = errors + warnings;
    m_problemsDock->setWindowTitle(total == 0 ? tr("PROBLEMS")
                                              : tr("PROBLEMS (%1)").arg(total));
    m_problemsTree->setToolTip(tr("%1 error(s), %2 warning(s)").arg(errors).arg(warnings));
}

void MainWindow::updateOutline(CodeEditor *editor)
{
    if (!m_outlineTree) return;
    m_outlineTree->clear();
    if (!editor || editor->filePath().isEmpty()) return;
    const auto symbols = m_languageManager->documentSymbols(editor->filePath(), editor->toPlainText());
    QList<QTreeWidgetItem *> parents;
    for (const auto &symbol : symbols) {
        while (parents.size() > symbol.depth) parents.removeLast();
        QTreeWidgetItem *item = parents.isEmpty()
            ? new QTreeWidgetItem(m_outlineTree) : new QTreeWidgetItem(parents.last());
        item->setText(0, symbol.name);
        item->setText(1, symbol.kind);
        item->setData(0, Qt::UserRole, symbol.line);
        item->setData(1, Qt::UserRole, symbol.column);
        if (symbol.kind == QStringLiteral("class") || symbol.kind == QStringLiteral("struct")
            || symbol.kind == QStringLiteral("namespace")) {
            while (parents.size() < symbol.depth) parents.push_back(item);
            parents.push_back(item);
        }
    }
    m_outlineTree->expandAll();
}

void MainWindow::managePlugins()
{
    PluginManagerDialog dialog(m_languageManager, this);
    dialog.exec();
}

void MainWindow::manageExtensions()
{
    ExtensionManagerDialog dialog(m_extensionManager, this);
    dialog.exec();
}

void MainWindow::quickOpen()
{
    if (m_projectDirectory.isEmpty()) { chooseProject(); return; }
    QuickOpenDialog dialog(m_projectDirectory, this);
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedFile().isEmpty())
        openFile(dialog.selectedFile());
}

void MainWindow::showProjectSearch()
{
    if (m_projectDirectory.isEmpty()) { chooseProject(); if (m_projectDirectory.isEmpty()) return; }
    if (!m_searchDock) {
        m_projectSearch = new ProjectSearchWidget(this);
        m_searchDock = new QDockWidget(tr("SEARCH"), this);
        m_searchDock->setObjectName(QStringLiteral("searchDock"));
        m_searchDock->setWidget(m_projectSearch);
        addDockWidget(Qt::BottomDockWidgetArea, m_searchDock);
        connect(m_projectSearch, &ProjectSearchWidget::resultActivated, this,
                [this](const QString &path, int line, int column) {
            openFile(path);
            if (auto *editor = currentEditor()) {
                const QTextBlock block = editor->document()->findBlockByNumber(line);
                if (block.isValid()) {
                    QTextCursor cursor(block);
                    cursor.setPosition(block.position() + column);
                    editor->setTextCursor(cursor);
                    editor->setFocus();
                }
            }
        });
    }
    m_projectSearch->setRootPath(m_projectDirectory);
    m_searchDock->show();
    m_searchDock->raise();
    m_projectSearch->focusSearch();
}

void MainWindow::splitEditorRight()
{
    if (!m_secondaryTabs) {
        m_secondaryTabs = new FileTabWidget(m_editorSplitter);
        m_secondaryTabs->setObjectName(QStringLiteral("secondaryEditorTabs"));
        configureEditorGroup(m_secondaryTabs);
        m_editorSplitter->addWidget(m_secondaryTabs);
        m_editorSplitter->setSizes({1, 1});
    }
    QTabWidget *source = m_activeTabs ? m_activeTabs : m_tabs;
    if (source == m_secondaryTabs || source->currentIndex() < 0) {
        m_activeTabs = m_secondaryTabs;
        if (m_secondaryTabs->count() == 0) addEditor();
        return;
    }
    const int sourceIndex = source->currentIndex();
    QWidget *widget = source->widget(sourceIndex);
    const QString title = source->tabText(sourceIndex);
    const QString tooltip = source->tabToolTip(sourceIndex);
    source->removeTab(sourceIndex);
    const int targetIndex = m_secondaryTabs->addTab(widget, title);
    m_secondaryTabs->setTabToolTip(targetIndex, tooltip);
    installTabCloseButton(m_secondaryTabs, targetIndex);
    m_secondaryTabs->setCurrentIndex(targetIndex);
    if (source->count() == 0) {
        m_activeTabs = source;
        addEditor();
    }
    m_activeTabs = m_secondaryTabs;
    widget->setFocus();
}

void MainWindow::showKeybindings()
{
    KeybindingsDialog dialog(findChildren<QAction *>(), this);
    dialog.exec();
}

void MainWindow::applyStoredKeybindings()
{
    QSettings settings;
    for (QAction *action : findChildren<QAction *>()) {
        if (!action || action->objectName().isEmpty()) continue;
        const QString key = QStringLiteral("keybindings/") + action->objectName();
        if (settings.contains(key))
            action->setShortcut(QKeySequence(settings.value(key).toString()));
    }
}

TerminalWidget *MainWindow::createTerminal(const QString &title, const QString &program,
                                           const QStringList &arguments)
{
    auto *terminal = new TerminalWidget(m_terminalTabs, program, arguments);
    terminal->setWorkingDirectory(m_projectDirectory.isEmpty() ? QDir::currentPath()
                                                                : m_projectDirectory);
    const int index = m_terminalTabs->addTab(terminal, title);
    connectTerminal(terminal);
    m_terminalTabs->setCurrentIndex(index);
    updateTerminalCloseButtons();
    m_terminal = terminal;
    if (m_servicesStarted) terminal->startDeferred();
    m_terminalDock->show();
    m_terminalDock->raise();
    return terminal;
}

void MainWindow::connectTerminal(TerminalWidget *terminal)
{
    connect(terminal, &TerminalWidget::terminateAvailabilityChanged, this,
            [this, terminal](bool available) {
        if (terminal == m_terminal) m_terminalTerminateButton->setEnabled(available);
    });
}

void MainWindow::updateTerminalCloseButtons()
{
    for (int index = 0; index < m_terminalTabs->count(); ++index) {
        QWidget *oldButton = m_terminalTabs->tabBar()->tabButton(index, QTabBar::RightSide);
        m_terminalTabs->tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
        if (oldButton) oldButton->deleteLater();
        if (m_terminalTabs->count() <= 1) continue;
        auto *button = new QToolButton(m_terminalTabs->tabBar());
        button->setObjectName(QStringLiteral("editorTabCloseButton"));
        button->setText(QStringLiteral("\u00d7"));
        button->setToolTip(tr("Close terminal"));
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(18, 18);
        m_terminalTabs->tabBar()->setTabButton(index, QTabBar::RightSide, button);
        connect(button, &QToolButton::clicked, this, [this, button] {
            for (int tab = 0; tab < m_terminalTabs->count(); ++tab) {
                if (m_terminalTabs->tabBar()->tabButton(tab, QTabBar::RightSide) == button) {
                    closeTerminalTab(tab);
                    return;
                }
            }
        });
    }
}

void MainWindow::closeTerminalTab(int index)
{
    if (m_terminalTabs->count() <= 1 || index < 0 || index >= m_terminalTabs->count()) return;
    QWidget *widget = m_terminalTabs->widget(index);
    m_terminalTabs->removeTab(index);
    widget->deleteLater();
    updateTerminalCloseButtons();
}

void MainWindow::newTerminal()
{
    createTerminal(tr("Terminal %1").arg(m_terminalTabs->count() + 1));
}

void MainWindow::newPowerShellTerminal()
{
#ifdef Q_OS_WIN
    createTerminal(tr("PowerShell %1").arg(m_terminalTabs->count() + 1),
                   QStringLiteral("powershell.exe"), {QStringLiteral("-NoLogo")});
#else
    newTerminal();
#endif
}

void MainWindow::runCurrentFile()
{
    auto *editor = currentEditor();
    if (!editor) return;
    if (editor->filePath().isEmpty() || editor->document()->isModified()) {
        if (!saveEditor(editor)) return;
    }
    const auto configuration = m_languageManager->runConfiguration(editor->filePath());
    if (!configuration.isValid()) {
        QMessageBox::information(this, tr("Cannot Run File"),
            tr("The %1 plugin does not provide a runner for this file.")
                .arg(m_languageManager->languageForFile(editor->filePath())));
        return;
    }
    m_terminalDock->show();
    m_terminalDock->raise();
    m_terminal->executeProgram(configuration.program, configuration.arguments,
                               configuration.workingDirectory);
}

void MainWindow::updateRunAction()
{
    if (!m_runAction) return;
    const auto *editor = currentEditor();
    const bool supported = editor && !editor->filePath().isEmpty()
        && m_languageManager->runConfiguration(editor->filePath()).isValid();
    m_runAction->setVisible(supported);
}

QIcon MainWindow::createRunIcon()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#3fb950")));
    QPolygonF triangle;
    triangle << QPointF(7.0, 4.0) << QPointF(19.0, 12.0) << QPointF(7.0, 20.0);
    painter.drawPolygon(triangle);
    return QIcon(pixmap);
}

bool MainWindow::confirmCloseEditor(CodeEditor *editor)
{
    if (!editor || !editor->document()->isModified()) return true;
    const QString name = editor->filePath().isEmpty() ? tr("Untitled") : QFileInfo(editor->filePath()).fileName();
    const auto answer = QMessageBox::warning(this, tr("Unsaved Changes"),
        tr("Save changes to %1?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    return answer == QMessageBox::Discard || saveEditor(editor);
}

void MainWindow::closeTab(int index)
{
    closeTabIn(m_tabs, index);
}

void MainWindow::closeTabIn(QTabWidget *tabs, int index)
{
    auto *editor = editorAt(tabs, index);
    if (!confirmCloseEditor(editor)) return;
    tabs->removeTab(index);
    editor->deleteLater();
    if (tabs->count() == 0) {
        m_activeTabs = tabs;
        if (m_findBar) m_findBar->hide();
        updateRunAction();
        updateProblems(nullptr);
        updateOutline(nullptr);
        if (m_cursorStatus) m_cursorStatus->clear();
        updateLanguageStatusWidgets();
    }
}

void MainWindow::moveEditorToGroup(const QString &path, QTabWidget *destination)
{
    if (path.isEmpty() || !destination) return;
    const auto [source, sourceIndex] = findEditorLocation(path);
    if (!source || source == destination || sourceIndex < 0) return;
    auto *editor = editorAt(source, sourceIndex);
    if (!editor) return;

    const QString title = source->tabText(sourceIndex);
    const QString tooltip = source->tabToolTip(sourceIndex);
    if (QWidget *button = source->tabBar()->tabButton(sourceIndex, QTabBar::RightSide)) {
        source->tabBar()->setTabButton(sourceIndex, QTabBar::RightSide, nullptr);
        button->deleteLater();
    }
    source->removeTab(sourceIndex);
    const int destinationIndex = destination->addTab(editor, title);
    destination->setTabToolTip(destinationIndex, tooltip);
    installTabCloseButton(destination, destinationIndex);
    destination->setCurrentIndex(destinationIndex);

    m_activeTabs = destination;
    editor->setFocus();
    updateRunAction();
    updateProblems(editor);
    updateOutline(editor);
}

void MainWindow::saveSession()
{
    QVariantList documents;
    const auto groups = editorGroups();
    for (int group = 0; group < groups.size(); ++group) {
        QTabWidget *tabs = groups.at(group);
        if (tabs->count() == 0) {
            QVariantMap emptyGroup;
            emptyGroup.insert(QStringLiteral("group"), group);
            emptyGroup.insert(QStringLiteral("emptyGroup"), true);
            documents.push_back(emptyGroup);
            continue;
        }
        for (int index = 0; index < tabs->count(); ++index) {
            auto *editor = editorAt(tabs, index);
            if (!editor) continue;
            QVariantMap document;
            document.insert(QStringLiteral("path"), editor->filePath());
            document.insert(QStringLiteral("title"), tabs->tabText(index).remove(QLatin1Char('*')));
            document.insert(QStringLiteral("group"), group);
            document.insert(QStringLiteral("cursor"), editor->textCursor().position());
            document.insert(QStringLiteral("modified"), editor->document()->isModified());
            if (editor->filePath().isEmpty() || editor->document()->isModified())
                document.insert(QStringLiteral("text"), editor->toPlainText());
            documents.push_back(document);
        }
    }
    QSettings settings;
    settings.setValue(QStringLiteral("session/project"), m_projectDirectory);
    settings.setValue(QStringLiteral("session/documents"), documents);
    settings.setValue(QStringLiteral("session/activeGroup"), m_activeTabs == m_secondaryTabs ? 1 : 0);
    settings.setValue(QStringLiteral("session/primaryIndex"), m_tabs->currentIndex());
    settings.setValue(QStringLiteral("session/secondaryIndex"),
                      m_secondaryTabs ? m_secondaryTabs->currentIndex() : -1);
    settings.setValue(QStringLiteral("session/editorSplitter"), m_editorSplitter->saveState());
}

void MainWindow::restoreSession()
{
    QSettings settings;
    const QString project = settings.value(QStringLiteral("session/project")).toString();
    if (QFileInfo(project).isDir()) openProject(project);
    m_pendingSessionDocuments = settings.value(QStringLiteral("session/documents")).toList();
    if (m_pendingSessionDocuments.isEmpty()) return;
    while (m_tabs->count() > 0) {
        QWidget *widget = m_tabs->widget(0);
        m_tabs->removeTab(0);
        widget->deleteLater();
    }
    m_sessionDocumentIndex = 0;
    m_sessionPrimaryIndex = settings.value(QStringLiteral("session/primaryIndex"), 0).toInt();
    m_sessionSecondaryIndex = settings.value(QStringLiteral("session/secondaryIndex"), 0).toInt();
    m_sessionActiveGroup = settings.value(QStringLiteral("session/activeGroup"), 0).toInt();
    m_sessionSplitterState = settings.value(QStringLiteral("session/editorSplitter")).toByteArray();
    restoreNextSessionDocument();
}

void MainWindow::restoreNextSessionDocument()
{
    if (m_sessionDocumentIndex >= m_pendingSessionDocuments.size()) {
        m_tabs->setCurrentIndex(m_sessionPrimaryIndex);
        if (m_secondaryTabs) m_secondaryTabs->setCurrentIndex(m_sessionSecondaryIndex);
        m_activeTabs = m_sessionActiveGroup == 1 && m_secondaryTabs ? m_secondaryTabs : m_tabs;
        m_editorSplitter->restoreState(m_sessionSplitterState);
        m_pendingSessionDocuments.clear();
        return;
    }

    const QVariantMap document = m_pendingSessionDocuments.at(m_sessionDocumentIndex++).toMap();
    const int group = document.value(QStringLiteral("group")).toInt();
    if (group == 1 && !m_secondaryTabs) {
        m_secondaryTabs = new FileTabWidget(m_editorSplitter);
        m_secondaryTabs->setObjectName(QStringLiteral("secondaryEditorTabs"));
        configureEditorGroup(m_secondaryTabs);
        m_editorSplitter->addWidget(m_secondaryTabs);
    }
    m_activeTabs = group == 1 && m_secondaryTabs ? m_secondaryTabs : m_tabs;
    if (document.value(QStringLiteral("emptyGroup")).toBool()) {
        QTimer::singleShot(0, this, &MainWindow::restoreNextSessionDocument);
        return;
    }
    auto *editor = addEditor(document.value(QStringLiteral("title"), tr("Untitled")).toString());
    const QString path = document.value(QStringLiteral("path")).toString();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QString error;
        if (editor->loadFromFile(path, &error))
            editor->setSyntaxHighlighter(m_languageManager->createHighlighter(path, editor->document()));
    }
    if (document.contains(QStringLiteral("text"))) {
        editor->setPlainText(document.value(QStringLiteral("text")).toString());
        editor->document()->setModified(document.value(QStringLiteral("modified")).toBool());
    }
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(qBound(0, document.value(QStringLiteral("cursor")).toInt(),
                              editor->document()->characterCount() - 1));
    editor->setTextCursor(cursor);
    updateTabTitle(editor);
    updateLanguageFeatures(editor);
    QTimer::singleShot(0, this, &MainWindow::restoreNextSessionDocument);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSession();
    QSettings settings;
    QStringList visibleExtensionViews;
    for (QDockWidget *dock : findChildren<QDockWidget *>()) {
        const QString viewId = dock->property("extensionViewId").toString();
        if (!viewId.isEmpty() && dock->isVisible()) visibleExtensionViews.push_back(viewId);
    }
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("window/visibleExtensionViews"), visibleExtensionViews);
    event->accept();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editorSplitter && event->type() == QEvent::Resize
        && m_findBar && m_findBar->isVisible()) {
        QTimer::singleShot(0, this, &MainWindow::positionFindBar);
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    else QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    bool opened = false;
    for (const QUrl &url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isFile()) {
            openFile(path);
            opened = true;
        }
    }
    if (opened) event->acceptProposedAction();
    else QMainWindow::dropEvent(event);
}
