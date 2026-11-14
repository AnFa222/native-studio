#include "mainwindow.h"

#include "codeeditor.h"
#include "languagemanager.h"
#include "pluginmanagerdialog.h"
#include "projectexplorer.h"
#include "terminalwidget.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeView>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabs(new QTabWidget(this))
    , m_projectExplorer(new ProjectExplorer(this))
    , m_languageManager(new LanguageManager(this))
    , m_terminal(new TerminalWidget(this))
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("Native Studio"));
    resize(1200, 760);

    m_tabs->setObjectName(QStringLiteral("editorTabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] { updateRunAction(); });

    connect(m_projectExplorer, &ProjectExplorer::fileActivated, this, &MainWindow::openFile);

    auto *dock = new QDockWidget(tr("EXPLORER"), this);
    dock->setObjectName(QStringLiteral("projectExplorerDock"));
    dock->setWidget(m_projectExplorer);
    dock->setMinimumWidth(260);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    m_terminalDock = new QDockWidget(tr("TERMINAL"), this);
    m_terminalDock->setObjectName(QStringLiteral("terminalDock"));
    m_terminalDock->setWidget(m_terminal);
    m_terminalDock->setMinimumHeight(160);
    addDockWidget(Qt::BottomDockWidgetArea, m_terminalDock);

    createActions();
    applyTheme();
    m_languageManager->loadDefaultPlugins();
    statusBar()->showMessage(tr("Ready — %1 language plugin(s) loaded")
                                 .arg(m_languageManager->languages().size()));
    addEditor();
}

QTabWidget *MainWindow::editorTabs() const { return m_tabs; }
QTreeView *MainWindow::projectTree() const { return m_projectExplorer->treeView(); }
ProjectExplorer *MainWindow::projectExplorer() const { return m_projectExplorer; }
LanguageManager *MainWindow::languageManager() const { return m_languageManager; }
TerminalWidget *MainWindow::terminal() const { return m_terminal; }

void MainWindow::createActions()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *projectMenu = menuBar()->addMenu(tr("&Project"));
    auto *pluginsMenu = menuBar()->addMenu(tr("&Plugins"));
    auto *runMenu = menuBar()->addMenu(tr("&Run"));
    auto *viewMenu = menuBar()->addMenu(tr("&View"));
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
    addAction(tr("&New"), QKeySequence::New, this, &MainWindow::createFile);
    addAction(tr("&Open File..."), QKeySequence::Open, this, &MainWindow::chooseFile);
    fileMenu->addSeparator();
    addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::saveCurrentFile);
    addAction(tr("Save &As..."), QKeySequence::SaveAs, this, &MainWindow::saveCurrentFileAs);
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

    auto *openProjectAction = projectMenu->addAction(tr("&Open Project..."), this, &MainWindow::chooseProject);
    openProjectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    pluginsMenu->addAction(tr("Manage Plugins…"), this, &MainWindow::managePlugins);
    pluginsMenu->addAction(tr("Rescan Language Plugins"), this, &MainWindow::reloadLanguagePlugins);
    runMenu->addAction(m_runAction);
    viewMenu->addAction(m_terminalDock->toggleViewAction());
}

void MainWindow::applyTheme()
{
    setStyleSheet(QStringLiteral(R"(
        QMainWindow { background: #1e1e1e; }
        QMenuBar, QMenu, QToolBar, QStatusBar { background: #252526; color: #dddddd; }
        QMenuBar::item:selected, QMenu::item:selected { background: #094771; }
        QToolBar { border: 0; spacing: 4px; padding: 3px; }
        QDockWidget { color: #cccccc; font-weight: 600; }
        QDockWidget::title { background: #181818; padding: 8px 10px; font-size: 11px; }
        QLabel#workspaceLabel { color: #cccccc; font-size: 11px; font-weight: 700; }
        QToolButton { color: #cccccc; background: transparent; border: 0; border-radius: 3px; }
        QToolButton:hover { background: #2a2d2e; }
        QTreeView, QPlainTextEdit { background: #1e1e1e; color: #d4d4d4; border: 0; selection-background-color: #264f78; }
        QLineEdit { background: #1e1e1e; color: #d4d4d4; border: 1px solid #3c3c3c; padding: 4px; }
        QTreeView::item { height: 22px; border: 0; }
        QTreeView::item:hover { background: #2a2d2e; }
        QTreeView::branch { background: #1e1e1e; }
        QTabWidget::pane { border: 0; }
        QTabBar::tab { background: #2d2d2d; color: #aaaaaa; padding: 8px 14px; border-right: 1px solid #1e1e1e; }
        QTabBar::tab:selected { background: #1e1e1e; color: #ffffff; border-top: 2px solid #007acc; }
    )"));
}

CodeEditor *MainWindow::addEditor(const QString &title)
{
    auto *editor = new CodeEditor(m_tabs);
    const int index = m_tabs->addTab(editor, title);
    m_tabs->setCurrentIndex(index);
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor] { updateTabTitle(editor); });
    connect(editor, &CodeEditor::cursorPositionDescriptionChanged,
            statusBar(), [this](const QString &message) { statusBar()->showMessage(message); });
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

void MainWindow::openProject(const QString &directory)
{
    const QFileInfo info(directory);
    if (!info.isDir()) return;
    m_projectDirectory = info.absoluteFilePath();
    m_projectExplorer->setRootPath(m_projectDirectory);
    m_terminal->setWorkingDirectory(m_projectDirectory);
    setWindowTitle(tr("%1 — Native Studio").arg(info.fileName()));
    statusBar()->showMessage(tr("Opened project %1").arg(QDir::toNativeSeparators(m_projectDirectory)), 4000);
}

int MainWindow::findEditor(const QString &path) const
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    for (int index = 0; index < m_tabs->count(); ++index) {
        const auto *editor = editorAt(index);
        if (editor && QFileInfo(editor->filePath()).canonicalFilePath() == canonical) return index;
    }
    return -1;
}

void MainWindow::openFile(const QString &path)
{
    const int existing = findEditor(path);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return;
    }

    CodeEditor *editor = nullptr;
    if (m_tabs->count() == 1 && editorAt(0)->filePath().isEmpty()
        && !editorAt(0)->document()->isModified() && editorAt(0)->document()->isEmpty()) {
        editor = editorAt(0);
    } else {
        editor = addEditor();
    }

    QString error;
    if (!editor->loadFromFile(path, &error)) {
        QMessageBox::critical(this, tr("Could Not Open File"), tr("%1\n\n%2").arg(path, error));
        if (editor->filePath().isEmpty() && editor->document()->isEmpty() && m_tabs->count() > 1) {
            m_tabs->removeTab(m_tabs->indexOf(editor));
            editor->deleteLater();
        }
        return;
    }
    editor->setSyntaxHighlighter(m_languageManager->createHighlighter(path, editor->document()));
    updateTabTitle(editor);
    updateRunAction();
    statusBar()->showMessage(tr("%1 — %2")
                                 .arg(QDir::toNativeSeparators(path),
                                      m_languageManager->languageForFile(path)), 3000);
}

CodeEditor *MainWindow::currentEditor() const { return editorAt(m_tabs->currentIndex()); }
CodeEditor *MainWindow::editorAt(int index) const { return qobject_cast<CodeEditor *>(m_tabs->widget(index)); }

void MainWindow::updateTabTitle(CodeEditor *editor)
{
    const int index = m_tabs->indexOf(editor);
    if (index < 0) return;
    QString title = editor->filePath().isEmpty() ? tr("Untitled") : QFileInfo(editor->filePath()).fileName();
    if (editor->document()->isModified()) title += QLatin1Char('*');
    m_tabs->setTabText(index, title);
    m_tabs->setTabToolTip(index, editor->filePath());
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
    QString error;
    if (!editor->saveToFile(path, &error)) {
        QMessageBox::critical(this, tr("Could Not Save File"), tr("%1\n\n%2").arg(path, error));
        return false;
    }
    editor->setSyntaxHighlighter(m_languageManager->createHighlighter(path, editor->document()));
    updateTabTitle(editor);
    updateRunAction();
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(path)), 3000);
    return true;
}

bool MainWindow::saveCurrentFile() { return saveEditor(currentEditor()); }
bool MainWindow::saveCurrentFileAs() { return saveEditor(currentEditor(), true); }

void MainWindow::reloadLanguagePlugins()
{
    const int loaded = m_languageManager->loadDefaultPlugins();
    statusBar()->showMessage(tr("Loaded %1 new plugin(s); %2 language(s) available")
                                 .arg(loaded).arg(m_languageManager->languages().size()), 4000);
}

void MainWindow::managePlugins()
{
    PluginManagerDialog dialog(m_languageManager, this);
    dialog.exec();
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
    auto *editor = editorAt(index);
    if (!confirmCloseEditor(editor)) return;
    m_tabs->removeTab(index);
    editor->deleteLater();
    if (m_tabs->count() == 0) addEditor();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int index = 0; index < m_tabs->count(); ++index) {
        if (!confirmCloseEditor(editorAt(index))) {
            event->ignore();
            return;
        }
    }
    event->accept();
}
