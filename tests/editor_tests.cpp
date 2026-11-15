#include "codeeditor.h"
#include "extensionmanager.h"
#include "extensionmanagerdialog.h"
#include "languagemanager.h"
#include "keybindingsdialog.h"
#include "mainwindow.h"
#include "pluginmanagerdialog.h"
#include "projectexplorer.h"
#include "settingsdialog.h"
#include "terminalwidget.h"

#include <QFile>
#include <QAction>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QCompleter>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QStatusBar>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTabBar>
#include <QTest>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QTreeWidget>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>

namespace {
class SettingGuard
{
public:
    explicit SettingGuard(QString key)
        : m_key(std::move(key)), m_existed(QSettings().contains(m_key)),
          m_value(QSettings().value(m_key))
    {
    }
    ~SettingGuard()
    {
        if (m_existed) QSettings().setValue(m_key, m_value);
        else QSettings().remove(m_key);
    }

private:
    QString m_key;
    bool m_existed = false;
    QVariant m_value;
};

class ApplicationSettingsGuard
{
public:
    ApplicationSettingsGuard()
        : m_organization(QCoreApplication::organizationName()),
          m_application(QCoreApplication::applicationName()),
          m_format(QSettings::defaultFormat())
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir(QDir::tempPath()).filePath(QStringLiteral("native-studio-tests")));
        QCoreApplication::setOrganizationName(QStringLiteral("NativeStudioTests"));
        QCoreApplication::setApplicationName(QStringLiteral("ThemeSettings"));
    }
    ~ApplicationSettingsGuard()
    {
        QCoreApplication::setOrganizationName(m_organization);
        QCoreApplication::setApplicationName(m_application);
        QSettings::setDefaultFormat(m_format);
    }

private:
    QString m_organization;
    QString m_application;
    QSettings::Format m_format;
};
}

class EditorTests final : public QObject
{
    Q_OBJECT

private slots:
    void startsWithOneUntitledEditor();
    void loadsServicesIncrementally();
    void roundTripsUtf8File();
    void openingSameFileReusesTab();
    void highlightsCppKeywords();
    void explorerShowsEveryFileType();
    void loadsPythonPlugin();
    void unknownLanguageUsesPlainText();
    void terminalRunsProgram();
    void terminalAcceptsPythonInput();
    void terminalTerminatesRunningProgram();
    void runActionTracksSupportedFile();
    void lineNumberGutterTracksDocument();
    void pluginExplorerHasMarketplaceControls();
    void loadsNativeBinaryExtension();
    void codeTidyFormatsDocumentAndSave();
    void extensionManagerShowsManifestControls();
    void settingsCenterIncludesCoreAndPluginPages();
    void lightThemeUsesDarkText();
    void darkThemeKeepsCompactChrome();
    void languagePluginsProvideCompletions();
    void languagePluginsDetectErrors();
    void pythonAnalyzerFindsSemanticProblems();
    void languagePluginsAcceptValidSyntax();
    void editorAcceptsPopupCompletion();
    void analysisRefreshKeepsCompletionOpen();
    void backspaceRestoresCompletionSuggestions();
    void escapeThenEnterDoesNotAcceptCompletion();
    void autoClosesAndKeepsPairsOnBackspace();
    void autoIndentsUsingLanguagePlugins();
    void problemsPanelTracksDiagnostics();
    void nativeWorkspaceFeaturesAreLazyAndAvailable();
    void closingLastFileShowsEmptyEditorState();
    void findAndReplaceCurrentDocument();
    void autoSaveWritesNamedFiles();
    void restoresExtensionDockLayoutAfterDeferredLoad();
    void gitChangesViewGroupsRepositoryState();
};

void EditorTests::startsWithOneUntitledEditor()
{
    MainWindow window;
    QCOMPARE(window.editorTabs()->count(), 1);
    QCOMPARE(window.editorTabs()->tabText(0), QStringLiteral("Untitled"));
    QVERIFY(window.editorTabs()->isMovable());
    QVERIFY(window.projectTree() != nullptr);
    QVERIFY(window.languageManager()->languages().isEmpty());
    QVERIFY(window.extensionManager()->loadedExtensions().isEmpty());
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("projectSearchWidget")) == nullptr);
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("gitWidget")) == nullptr);
    QVERIFY(window.findChild<QTabWidget *>(QStringLiteral("secondaryEditorTabs")) == nullptr);
    auto *closeButton = window.editorTabs()->tabBar()->tabButton(0, QTabBar::RightSide);
    QVERIFY(closeButton != nullptr);
    QCOMPARE(closeButton->objectName(), QStringLiteral("editorTabCloseButton"));
    auto *closeToolButton = qobject_cast<QToolButton *>(closeButton);
    QVERIFY(closeToolButton != nullptr);
    QCOMPARE(closeToolButton->text(), QStringLiteral("\u00d7"));
}

void EditorTests::loadsServicesIncrementally()
{
    MainWindow window;
    QSignalSpy finished(&window, &MainWindow::servicesLoaded);
    window.startDeferredServices();

    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    QVERIFY(window.languageManager()->languages().contains(QStringLiteral("Python")));
    const auto extensions = window.extensionManager()->loadedExtensions();
    QVERIFY(std::any_of(extensions.cbegin(), extensions.cend(), [](const auto &extension) {
        return extension.id == QStringLiteral("dev.nativestudio.hello");
    }));
    QVERIFY(std::any_of(extensions.cbegin(), extensions.cend(), [](const auto &extension) {
        return extension.id == QStringLiteral("dev.nativestudio.code_tidy");
    }));
    QVERIFY(std::any_of(extensions.cbegin(), extensions.cend(), [](const auto &extension) {
        return extension.id == QStringLiteral("dev.nativestudio.git");
    }));
}

void EditorTests::roundTripsUtf8File()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("hello.cpp"));

    CodeEditor editor;
    editor.setPlainText(QString::fromUtf8("// café\nint main() { return 0; }\n"));
    QString error;
    QVERIFY2(editor.saveToFile(path, &error), qPrintable(error));
    QVERIFY(!editor.document()->isModified());

    CodeEditor loaded;
    QVERIFY2(loaded.loadFromFile(path, &error), qPrintable(error));
    QCOMPARE(loaded.toPlainText(), editor.toPlainText());
    QCOMPARE(loaded.filePath(), QFileInfo(path).absoluteFilePath());
}

void EditorTests::openingSameFileReusesTab()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("sample.cpp"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("int value = 42;\n"), 16);
    file.close();

    MainWindow window;
    window.loadServicesNow();
    window.openFile(path);
    window.openFile(path);
    QCOMPARE(window.editorTabs()->count(), 1);
    QCOMPARE(qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget())->toPlainText(),
             QStringLiteral("int value = 42;\n"));
}

void EditorTests::highlightsCppKeywords()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("language.cpp"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("constexpr int answer = 42;");
    file.close();

    MainWindow window;
    window.loadServicesNow();
    QVERIFY(window.languageManager()->languages().contains(QStringLiteral("C++")));
    window.openFile(path);
    auto *interpreter = window.findChild<QToolButton *>(
        QStringLiteral("pythonInterpreterSelector"));
    QVERIFY(interpreter != nullptr);
    QVERIFY(interpreter->text().startsWith(QStringLiteral("Python:")));
    QCOMPARE(interpreter->parentWidget(), window.statusBar());
    auto *editor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(editor != nullptr);
    QCoreApplication::processEvents();
    const auto block = editor->document()->firstBlock();
    bool foundKeywordFormat = false;
    for (const auto &range : block.layout()->formats()) {
        if (range.start == 0 && range.length == 9 && range.format.fontWeight() >= QFont::DemiBold) {
            foundKeywordFormat = true;
        }
    }
    QVERIFY(foundKeywordFormat);
}

void EditorTests::explorerShowsEveryFileType()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile python(directory.filePath(QStringLiteral("tool.py")));
    QVERIFY(python.open(QIODevice::WriteOnly));
    python.write("print('native')\n");
    python.close();

    MainWindow window;
    window.openProject(directory.path());
    auto *explorer = window.projectExplorer();
    QCOMPARE(explorer->rootPath(), QFileInfo(directory.path()).absoluteFilePath());
    const QModelIndex root = explorer->model()->index(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(explorer->model()->rowCount(root) > 0, 3000);
    QVERIFY(explorer->model()->index(directory.filePath(QStringLiteral("tool.py"))).isValid());
}

void EditorTests::unknownLanguageUsesPlainText()
{
    MainWindow window;
    window.loadServicesNow();
    QCOMPARE(window.languageManager()->languageForFile(QStringLiteral("notes.xyz")),
             QStringLiteral("Plain Text"));
}

void EditorTests::loadsPythonPlugin()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("example.py"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("def greet(name):\n    print(f\"Hello {name}\")\n");
    file.close();

    MainWindow window;
    window.loadServicesNow();
    QVERIFY(window.languageManager()->languages().contains(QStringLiteral("Python")));
    QCOMPARE(window.languageManager()->languageForFile(path), QStringLiteral("Python"));
    const auto runConfiguration = window.languageManager()->runConfiguration(path);
    QVERIFY(runConfiguration.isValid());
    QVERIFY(runConfiguration.arguments.contains(QStringLiteral("-u")));
    QVERIFY(runConfiguration.arguments.contains(QFileInfo(path).absoluteFilePath()));
    window.openFile(path);
    auto *editor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(editor != nullptr);
    QCoreApplication::processEvents();

    const auto block = editor->document()->firstBlock();
    bool foundKeyword = false;
    for (const auto &range : block.layout()->formats()) {
        if (range.start == 0 && range.length == 3 && range.format.fontWeight() >= QFont::DemiBold) {
            foundKeyword = true;
        }
    }
    QVERIFY(foundKeyword);
}

void EditorTests::terminalRunsProgram()
{
    MainWindow window;
    window.loadServicesNow();
    QVERIFY(window.terminal()->findChild<QLineEdit *>(QStringLiteral("terminalInput")) == nullptr);
    QVERIFY(window.terminal()->findChild<QWidget *>(QStringLiteral("terminalHeader")) == nullptr);
    auto *terminalView = window.terminal()->findChild<QPlainTextEdit *>(
        QStringLiteral("terminalOutput"));
    auto *terminate = window.findChild<QToolButton *>(
        QStringLiteral("terminateTerminalButton"));
    QVERIFY(terminalView != nullptr);
    QVERIFY(terminalView->styleSheet().contains(QStringLiteral("#ffffff")));
    QVERIFY(terminate != nullptr);
    QVERIFY(terminate->text().isEmpty());
    QCOMPARE(terminate->size(), QSize(12, 12));
    auto *terminalTabs = window.findChild<QTabWidget *>(QStringLiteral("terminalTabs"));
    QVERIFY(terminalTabs != nullptr);
    QCOMPARE(terminalTabs->cornerWidget(Qt::TopRightCorner), terminate);
    QVERIFY(window.findChild<QAction *>(QStringLiteral("preferencesAction")) != nullptr);
    QSignalSpy finished(window.terminal(), &TerminalWidget::commandFinished);
#ifdef Q_OS_WIN
    window.terminal()->executeProgram(QStringLiteral("cmd.exe"),
                                      {QStringLiteral("/C"), QStringLiteral("echo terminal-ready")});
#else
    window.terminal()->executeProgram(QStringLiteral("/bin/sh"),
                                      {QStringLiteral("-c"), QStringLiteral("echo terminal-ready")});
#endif
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(window.terminal()->outputText().contains(QStringLiteral("terminal-ready")));
    terminate->click();
    QTRY_VERIFY_WITH_TIMEOUT(window.terminal()->outputText().contains(
        QStringLiteral("Terminal shell stopped")), 3000);
    QVERIFY(!window.terminal()->outputText().contains(QStringLiteral("Process crashed")));
}

void EditorTests::terminalAcceptsPythonInput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("interactive.py"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("name = input('Name: ')\nprint(f'Hello {name}', flush=True)\n");
    file.close();

    MainWindow window;
    window.loadServicesNow();
    window.show();
    const auto configuration = window.languageManager()->runConfiguration(path);
    QVERIFY(configuration.isValid());
    auto *view = window.terminal()->findChild<QPlainTextEdit *>(QStringLiteral("terminalOutput"));
    QVERIFY(view != nullptr);
    QSignalSpy finished(window.terminal(), &TerminalWidget::commandFinished);
    window.terminal()->executeProgram(configuration.program, configuration.arguments,
                                      configuration.workingDirectory);
    QTRY_VERIFY_WITH_TIMEOUT(window.terminal()->outputText().contains(QStringLiteral("Name: ")),
                             5000);
    view->setFocus();
    QTest::keyClicks(view, QStringLiteral("Native"));
    QTest::keyClick(view, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    QVERIFY(window.terminal()->outputText().contains(QStringLiteral("Hello Native")));
}

void EditorTests::terminalTerminatesRunningProgram()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("long_running.py"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("import time\nprint('waiting', flush=True)\ntime.sleep(30)\n");
    file.close();

    MainWindow window;
    window.loadServicesNow();
    const auto configuration = window.languageManager()->runConfiguration(path);
    auto *terminate = window.findChild<QToolButton *>(
        QStringLiteral("terminateTerminalButton"));
    QVERIFY(terminate != nullptr);
    QSignalSpy finished(window.terminal(), &TerminalWidget::commandFinished);
    window.terminal()->executeProgram(configuration.program, configuration.arguments,
                                      configuration.workingDirectory);
    QTRY_VERIFY_WITH_TIMEOUT(window.terminal()->outputText().contains(QStringLiteral("waiting")),
                             5000);
    QVERIFY(terminate->isEnabled());
    terminate->click();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    QVERIFY(window.terminal()->outputText().contains(QStringLiteral("Terminating")));
    QVERIFY(window.terminal()->outputText().contains(QStringLiteral("Process terminated")));
    QVERIFY(!window.terminal()->outputText().contains(QStringLiteral("Process crashed")));
}

void EditorTests::runActionTracksSupportedFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pythonPath = directory.filePath(QStringLiteral("supported.py"));
    const QString textPath = directory.filePath(QStringLiteral("unsupported.xyz"));
    for (const auto &path : {pythonPath, textPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("content\n");
    }

    MainWindow window;
    window.loadServicesNow();
    auto *runAction = window.findChild<QAction *>(QStringLiteral("runCurrentFileAction"));
    QVERIFY(runAction != nullptr);
    QVERIFY(!runAction->isVisible());

    window.openFile(pythonPath);
    QVERIFY(runAction->isVisible());
    window.openFile(textPath);
    QVERIFY(!runAction->isVisible());
}

void EditorTests::lineNumberGutterTracksDocument()
{
    CodeEditor editor;
    const int singleDigitWidth = editor.lineNumberAreaWidth();
    QStringList lines;
    for (int line = 1; line <= 120; ++line) lines.append(QStringLiteral("line %1").arg(line));
    editor.setPlainText(lines.join(QLatin1Char('\n')));

    QVERIFY(editor.findChild<QWidget *>(QStringLiteral("lineNumberArea")) != nullptr);
    QVERIFY(editor.lineNumberAreaWidth() > singleDigitWidth);
}

void EditorTests::pluginExplorerHasMarketplaceControls()
{
    MainWindow window;
    window.loadServicesNow();
    PluginManagerDialog dialog(window.languageManager());

    QCOMPARE(dialog.windowTitle(), QStringLiteral("Plugin Explorer"));
    auto *search = dialog.findChild<QLineEdit *>(QStringLiteral("pluginSearch"));
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("pluginTable"));
    auto *install = dialog.findChild<QPushButton *>(QStringLiteral("installPluginButton"));
    auto *refresh = dialog.findChild<QPushButton *>(QStringLiteral("refreshPluginCatalog"));
    QVERIFY(search != nullptr);
    QVERIFY(table != nullptr);
    QVERIFY(install != nullptr);
    QVERIFY(refresh != nullptr);
    QCOMPARE(table->columnCount(), 5);
    QVERIFY(table->rowCount() >= 2);
}

void EditorTests::loadsNativeBinaryExtension()
{
    MainWindow window;
    window.loadServicesNow();
    auto *manager = window.extensionManager();
    QVERIFY(manager != nullptr);
    const auto extensions = manager->loadedExtensions();
    QVERIFY(std::any_of(extensions.cbegin(), extensions.cend(), [](const auto &extension) {
        return extension.id == QStringLiteral("dev.nativestudio.hello");
    }));
    const auto installed = manager->installedExtensions();
    const auto installedHello = std::find_if(
        installed.cbegin(), installed.cend(), [](const auto &extension) {
            return extension.id == QStringLiteral("dev.nativestudio.hello");
        });
    QVERIFY(installedHello != installed.cend());
    QCOMPARE(installedHello->abiVersion, uint32_t(NS_EXTENSION_ABI_VERSION));
    QVERIFY(QFileInfo::exists(installedHello->manifestPath));
    QVERIFY(installedHello->loaded);
    QVERIFY(installedHello->bundled);
    QVERIFY(QDir::fromNativeSeparators(installedHello->manifestPath)
                .contains(QStringLiteral("/extensions/bundled/")));
    const QString commandId = QStringLiteral("dev.nativestudio.hello.say_hello");
    QVERIFY(manager->commandIds().contains(commandId));
    auto *action = window.findChild<QAction *>(QStringLiteral("extensionCommand.%1").arg(commandId));
    QVERIFY(action != nullptr);
    action->trigger();
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("native extension"),
                                                           Qt::CaseInsensitive));
    const QString insertId = QStringLiteral("dev.nativestudio.hello.insert_text");
    auto *insertAction = window.findChild<QAction *>(
        QStringLiteral("extensionCommand.%1").arg(insertId));
    QVERIFY(insertAction != nullptr);
    insertAction->trigger();
    auto *editor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(editor != nullptr);
    QCOMPARE(editor->toPlainText(), QStringLiteral("Hello from Native Hello"));
}

void EditorTests::codeTidyFormatsDocumentAndSave()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("tidy.cpp"));
    QFile seed(path);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write("int original;\n");
    seed.close();

    MainWindow window;
    window.loadServicesNow();
    auto *manager = window.extensionManager();
    const auto installed = manager->installedExtensions();
    const auto tidy = std::find_if(installed.cbegin(), installed.cend(), [](const auto &extension) {
        return extension.id == QStringLiteral("dev.nativestudio.code_tidy");
    });
    QVERIFY(tidy != installed.cend());
    QVERIFY(tidy->bundled);
    QVERIFY(tidy->loaded);
    QVERIFY(QDir::fromNativeSeparators(tidy->manifestPath)
                .contains(QStringLiteral("/extensions/bundled/")));
    QString removalError;
    QVERIFY(!manager->removeExtension(tidy->id, &removalError));
    QVERIFY(removalError.contains(QStringLiteral("disabled"), Qt::CaseInsensitive));
    QString updateError;
    QVERIFY(!manager->importExtension(tidy->manifestPath, &updateError));
    QVERIFY(updateError.contains(QStringLiteral("cannot be replaced"), Qt::CaseInsensitive));

    window.openFile(path);
    auto *editor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(editor != nullptr);
    editor->setPlainText(QStringLiteral("\tint value = 1;   \n\n\n"));
    const QString commandId = QStringLiteral("dev.nativestudio.code_tidy.format_document");
    auto *formatAction = window.findChild<QAction *>(
        QStringLiteral("extensionCommand.%1").arg(commandId));
    QVERIFY(formatAction != nullptr);
    formatAction->trigger();
    QCOMPARE(editor->toPlainText(), QStringLiteral("    int value = 1;\n"));

    editor->setPlainText(QStringLiteral("\tint saved = 2; \t\n\n"));
    auto *saveAction = window.findChild<QAction *>(QStringLiteral("saveFileAction"));
    QVERIFY(saveAction != nullptr);
    saveAction->trigger();
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QString savedText = QString::fromUtf8(saved.readAll());
    savedText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QCOMPARE(savedText, QStringLiteral("    int saved = 2;\n"));

    const QString pythonPath = directory.filePath(QStringLiteral("tidy.py"));
    QFile pythonSeed(pythonPath);
    QVERIFY(pythonSeed.open(QIODevice::WriteOnly));
    pythonSeed.write("pass\n");
    pythonSeed.close();
    window.openFile(pythonPath);
    auto *pythonEditor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(pythonEditor != nullptr);
    pythonEditor->setPlainText(QStringLiteral(
        "import os\n"
        "from . import helper\n"
        "@decorator\n"
        "def greet( name:str=\"world\",age=1 ):\n"
        " print( \"hello\",name )\n"
        " if(age>0):\n"
        "\n"
        "\n"
        "  return [{\"age\":age,\"name\":name}]\n"
        "class Person:\n"
        " def speak( self ):\n"
        "  \"\"\"keep   this\n"
        " literal   spacing\n"
        "  \"\"\"\n"
        "  return \"ok\"\n"));
    formatAction->trigger();
    QCOMPARE(pythonEditor->toPlainText(), QStringLiteral(
        "import os\n"
        "from . import helper\n"
        "\n"
        "\n"
        "@decorator\n"
        "def greet(name: str = \"world\", age=1):\n"
        "    print(\"hello\", name)\n"
        "    if (age > 0):\n"
        "        return [{\"age\": age, \"name\": name}]\n"
        "\n"
        "\n"
        "class Person:\n"
        "\n"
        "    def speak(self):\n"
        "        \"\"\"keep   this\n"
        " literal   spacing\n"
        "  \"\"\"\n"
        "        return \"ok\"\n"));
}

void EditorTests::extensionManagerShowsManifestControls()
{
    MainWindow window;
    window.loadServicesNow();
    ExtensionManagerDialog dialog(window.extensionManager());

    QCOMPARE(dialog.windowTitle(), QStringLiteral("Native Extension Manager"));
    auto *search = dialog.findChild<QLineEdit *>(QStringLiteral("extensionSearch"));
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("extensionTable"));
    auto *import = dialog.findChild<QPushButton *>(QStringLiteral("importExtensionManifest"));
    auto *toggle = dialog.findChild<QPushButton *>(QStringLiteral("toggleExtension"));
    auto *remove = dialog.findChild<QPushButton *>(QStringLiteral("removeExtension"));
    auto *install = dialog.findChild<QPushButton *>(QStringLiteral("installExtensionButton"));
    auto *refreshStore = dialog.findChild<QPushButton *>(
        QStringLiteral("refreshExtensionCatalog"));
    QVERIFY(search != nullptr);
    QVERIFY(table != nullptr);
    QVERIFY(import != nullptr);
    QVERIFY(toggle != nullptr);
    QVERIFY(remove != nullptr);
    QVERIFY(install != nullptr);
    QVERIFY(refreshStore != nullptr);
    QCOMPARE(table->columnCount(), 6);
    QVERIFY(table->rowCount() >= 2);
    int bundledRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 1)->text() == QStringLiteral("dev.nativestudio.code_tidy")) {
            bundledRow = row;
            break;
        }
    }
    QVERIFY(bundledRow >= 0);
    table->selectRow(bundledRow);
    QCoreApplication::processEvents();
    QVERIFY(toggle->isEnabled());
    QVERIFY(!remove->isEnabled());
}

void EditorTests::settingsCenterIncludesCoreAndPluginPages()
{
    ApplicationSettingsGuard applicationSettings;
    const QString formatPath = QStringLiteral(
        "extensions/settings/dev.nativestudio.code_tidy/format_on_save");
    const QString indentPath = QStringLiteral(
        "extensions/settings/dev.nativestudio.code_tidy/python_indent_width");
    const QString gitPath = QStringLiteral(
        "extensions/settings/dev.nativestudio.git/git_path");
    SettingGuard formatGuard(formatPath);
    SettingGuard indentGuard(indentPath);
    SettingGuard gitGuard(gitPath);
    QSettings().remove(formatPath);
    QSettings().remove(indentPath);
    QSettings().remove(gitPath);

    MainWindow window;
    window.loadServicesNow();
    SettingsDialog dialog(window.languageManager(), window.extensionManager());

    QCOMPARE(dialog.windowTitle(), QStringLiteral("Settings"));
    auto *search = dialog.findChild<QLineEdit *>(QStringLiteral("settingsSearch"));
    auto *categories = dialog.findChild<QListWidget *>(QStringLiteral("settingsCategories"));
    QVERIFY(search != nullptr);
    QVERIFY(categories != nullptr);
    QVERIFY(categories->count() >= 5);
    QVERIFY(dialog.findChild<QComboBox *>(QStringLiteral("colorThemeSetting")) != nullptr);
    QVERIFY(dialog.findChild<QWidget *>(QStringLiteral("editorAutoClosePairsSetting")) != nullptr);
    QVERIFY(dialog.findChild<QWidget *>(QStringLiteral("editorAutoIndentSetting")) != nullptr);
    QVERIFY(dialog.findChild<QCheckBox *>(QStringLiteral("editorAutoSaveSetting")) != nullptr);
    QVERIFY(dialog.findChild<QSpinBox *>(QStringLiteral("editorAutoSaveDelaySetting")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("settingsManagePlugins")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("settingsManageExtensions")) != nullptr);
    QVERIFY(dialog.findChild<QWidget *>(QStringLiteral("pythonSettingsPage")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("selectPythonInterpreter")) != nullptr);
    QVERIFY(dialog.findChild<QWidget *>(QStringLiteral("terminalFollowThemeSetting")) != nullptr);
    QVERIFY(dialog.findChild<QWidget *>(
        QStringLiteral("extensionSettingsPage.dev.nativestudio.code_tidy")) != nullptr);
    auto *formatOnSave = dialog.findChild<QCheckBox *>(
        QStringLiteral("extensionSetting.dev.nativestudio.code_tidy.format_on_save"));
    QVERIFY(formatOnSave != nullptr);
    auto *pythonIndent = dialog.findChild<QSpinBox *>(
        QStringLiteral("extensionSetting.dev.nativestudio.code_tidy.python_indent_width"));
    QVERIFY(pythonIndent != nullptr);
    QCOMPARE(pythonIndent->value(), 4);
    auto *gitExecutable = dialog.findChild<QLineEdit *>(
        QStringLiteral("extensionSetting.dev.nativestudio.git.git_path"));
    QVERIFY(gitExecutable != nullptr);
    QCOMPARE(gitExecutable->text(), QStringLiteral("git"));

    search->setText(QStringLiteral("interpreter"));
    int visibleCategories = 0;
    for (int index = 0; index < categories->count(); ++index)
        visibleCategories += categories->item(index)->isHidden() ? 0 : 1;
    QCOMPARE(visibleCategories, 1);

    formatOnSave->setChecked(false);
    pythonIndent->setValue(2);
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(buttons != nullptr);
    buttons->button(QDialogButtonBox::Ok)->click();
    QCOMPARE(QSettings().value(formatPath).toString(), QStringLiteral("false"));
    QCOMPARE(QSettings().value(indentPath).toString(), QStringLiteral("2"));
}

void EditorTests::lightThemeUsesDarkText()
{
    ApplicationSettingsGuard applicationSettings;
    SettingGuard themeGuard(QStringLiteral("appearance/theme"));
    SettingGuard terminalThemeGuard(QStringLiteral("terminal/followTheme"));
    QSettings testSettings;
    testSettings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("light"));
    testSettings.setValue(QStringLiteral("terminal/followTheme"), true);
    testSettings.sync();
    QCOMPARE(testSettings.value(QStringLiteral("appearance/theme")).toString(),
             QStringLiteral("light"));

    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    QCOMPARE(QSettings().value(QStringLiteral("appearance/theme")).toString(),
             QStringLiteral("light"));
    QVERIFY(window.styleSheet().contains(QStringLiteral("QMainWindow, QDialog")));
    auto *editor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    auto *terminalView = window.terminal()->findChild<QPlainTextEdit *>(
        QStringLiteral("terminalOutput"));
    QVERIFY(editor != nullptr);
    QVERIFY(terminalView != nullptr);
    QCOMPARE(editor->palette().color(QPalette::Text), QColor(QStringLiteral("#24292f")));
    QVERIFY(terminalView->styleSheet().contains(QStringLiteral("color: #24292f")));
    QVERIFY(terminalView->styleSheet().contains(QStringLiteral("background: #ffffff")));
}

void EditorTests::darkThemeKeepsCompactChrome()
{
    ApplicationSettingsGuard applicationSettings;
    SettingGuard themeGuard(QStringLiteral("appearance/theme"));
    QSettings().setValue(QStringLiteral("appearance/theme"), QStringLiteral("dark"));

    MainWindow window;
    const QString style = window.styleSheet();
    QVERIFY(style.contains(QStringLiteral("QToolBar { background: #161b22")));
    QVERIFY(style.contains(QStringLiteral("QTabBar::tab { background: #161b22")));
    QVERIFY(!style.contains(QStringLiteral("QToolBar QToolButton { padding")));
    QVERIFY(!style.contains(QStringLiteral("spacing: 5px; padding: 5px 7px")));
    QVERIFY(!style.contains(QStringLiteral("padding: 9px 16px")));
    QVERIFY(style.contains(QStringLiteral("padding: 5px 10px")));
    QVERIFY(style.contains(QStringLiteral("QToolButton#editorTabCloseButton")));
}

void EditorTests::languagePluginsProvideCompletions()
{
    MainWindow window;
    window.loadServicesNow();
    const auto pythonItems = window.languageManager()->completionItems(
        QStringLiteral("example.py"),
        QStringLiteral("\"\"\"docstring_words should not complete\"\"\"\n"
                       "def custom_symbol():\n    real_value = 'hello_from_string'\n"
                       "    # comment_words should not complete\n    pri"));
    QVERIFY(pythonItems.contains(QStringLiteral("print")));
    QVERIFY(pythonItems.contains(QStringLiteral("custom_symbol")));
    QVERIFY(pythonItems.contains(QStringLiteral("real_value")));
    QVERIFY(!pythonItems.contains(QStringLiteral("hello_from_string")));
    QVERIFY(!pythonItems.contains(QStringLiteral("comment_words")));
    QVERIFY(!pythonItems.contains(QStringLiteral("docstring_words")));

    const auto cppItems = window.languageManager()->completionItems(
        QStringLiteral("example.cpp"),
        QStringLiteral("struct CustomWidget {};\nint real_value = 1;\n"
                       "const char *message = \"hello_from_string\";\n"
                       "const char *raw = R\"(raw_string_words)\";\n"
                       "// comment_words should not complete\n"
                       "/* block_comment_words should not complete */\nconste"));
    QVERIFY(cppItems.contains(QStringLiteral("constexpr")));
    QVERIFY(cppItems.contains(QStringLiteral("CustomWidget")));
    QVERIFY(cppItems.contains(QStringLiteral("real_value")));
    QVERIFY(!cppItems.contains(QStringLiteral("hello_from_string")));
    QVERIFY(!cppItems.contains(QStringLiteral("comment_words")));
    QVERIFY(!cppItems.contains(QStringLiteral("raw_string_words")));
    QVERIFY(!cppItems.contains(QStringLiteral("block_comment_words")));
}

void EditorTests::languagePluginsDetectErrors()
{
    MainWindow window;
    window.loadServicesNow();
    const auto pythonProblems = window.languageManager()->diagnostics(
        QStringLiteral("broken.py"),
        QStringLiteral("if {'key': 1}\n    pass\n"));
    QVERIFY(!pythonProblems.isEmpty());
    QVERIFY(std::any_of(pythonProblems.cbegin(), pythonProblems.cend(), [](const auto &problem) {
        return problem.message.contains(QStringLiteral("Expected ':'"));
    }));

    const auto indentationProblems = window.languageManager()->diagnostics(
        QStringLiteral("indentation.py"), QStringLiteral("def greet():\nprint('hello')\n"));
    QVERIFY(std::any_of(indentationProblems.cbegin(), indentationProblems.cend(), [](const auto &problem) {
        return problem.message.contains(QStringLiteral("Expected an indented block"));
    }));

    const auto cppProblems = window.languageManager()->diagnostics(
        QStringLiteral("broken.cpp"),
        QStringLiteral("#else\nint main() {\n    int value = 42\n"
                       "    if value > 0 {\n        return value\n    }\n}\n"));
    QVERIFY(!cppProblems.isEmpty());
    QVERIFY(std::any_of(cppProblems.cbegin(), cppProblems.cend(), [](const auto &problem) {
        return problem.message.contains(QStringLiteral("Expected ';'"));
    }));
    QVERIFY(std::any_of(cppProblems.cbegin(), cppProblems.cend(), [](const auto &problem) {
        return problem.message.contains(QStringLiteral("Expected '('"));
    }));
    QVERIFY(std::any_of(cppProblems.cbegin(), cppProblems.cend(), [](const auto &problem) {
        return problem.message.contains(QStringLiteral("no matching #if"));
    }));
}

void EditorTests::pythonAnalyzerFindsSemanticProblems()
{
    MainWindow window;
    window.loadServicesNow();
    const auto problems = window.languageManager()->diagnostics(
        QStringLiteral("analysis.py"),
        QStringLiteral("import os\n"
                       "\"floating string\"\n"
                       "def greet(unused, items=[]):\n"
                       "    value = 42\n"
                       "    print(missing_name)\n"
                       "    return 1\n"
                       "    print('unreachable')\n"));
    const auto hasMessage = [&problems](const QString &text) {
        return std::any_of(problems.cbegin(), problems.cend(), [&text](const auto &problem) {
            return problem.message.contains(text, Qt::CaseInsensitive);
        });
    };
    QVERIFY(hasMessage(QStringLiteral("Imported name 'os' is never used")));
    QVERIFY(hasMessage(QStringLiteral("Standalone string literal")));
    QVERIFY(hasMessage(QStringLiteral("Mutable default argument")));
    QVERIFY(hasMessage(QStringLiteral("Undefined name 'missing_name'")));
    QVERIFY(hasMessage(QStringLiteral("Local variable 'value'")));
    QVERIFY(hasMessage(QStringLiteral("Unreachable code")));
}

void EditorTests::languagePluginsAcceptValidSyntax()
{
    MainWindow window;
    window.loadServicesNow();
    const auto pythonProblems = window.languageManager()->diagnostics(
        QStringLiteral("valid.py"),
        QStringLiteral("def greet(\n"
                       "    name: str,\n"
                       ") -> dict:\n"
                       "    if name:\n"
                       "        return {'message': name}\n"
                       "    return None\n"));
    QVERIFY(pythonProblems.isEmpty());

    const auto cppProblems = window.languageManager()->diagnostics(
        QStringLiteral("valid.cpp"),
        QStringLiteral("#include <string>\n"
                       "int main(\n"
                       ")\n"
                       "{\n"
                       "    const std::string text = \"{ not code }\";\n"
                       "    // if broken words should be ignored\n"
                       "    return 0;\n"
                       "}\n"));
    QVERIFY(cppProblems.isEmpty());
}

void EditorTests::editorAcceptsPopupCompletion()
{
    CodeEditor editor;
    editor.setCompletionItems({QStringLiteral("print"), QStringLiteral("property")});
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents();

    QTest::keyClicks(&editor, QStringLiteral("p"));
    QVERIFY(editor.completionPopupVisible());
    QTest::keyClicks(&editor, QStringLiteral("r"));
    QVERIFY(editor.completionPopupVisible());

    // Simulate the delayed plugin-analysis pass. It discovers the partial
    // document identifier "pr", but must not collapse the active popup.
    editor.setCompletionItems({QStringLiteral("pr"), QStringLiteral("print"),
                               QStringLiteral("property")});
    QVERIFY(editor.completionPopupVisible());

    QTest::keyClicks(&editor, QStringLiteral("i"));
    QVERIFY(editor.completionPopupVisible());
    QTest::keyClicks(&editor, QStringLiteral("n"));
    QVERIFY(editor.completionPopupVisible());
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(editor.toPlainText(), QStringLiteral("print"));
}

void EditorTests::analysisRefreshKeepsCompletionOpen()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("completion.py"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    MainWindow window;
    window.loadServicesNow();
    window.openFile(path);
    window.show();
    auto *editor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(editor != nullptr);
    editor->setFocus();
    QCoreApplication::processEvents();

    QTest::keyClicks(editor, QStringLiteral("pr"));
    QTRY_VERIFY(editor->completionPopupVisible());
    QTest::qWait(350);
    QVERIFY(editor->completionPopupVisible());

    QTest::keyClicks(editor, QStringLiteral("i"));
    QTest::qWait(350);
    QVERIFY(editor->completionPopupVisible());
}

void EditorTests::backspaceRestoresCompletionSuggestions()
{
    CodeEditor editor;
    editor.setCompletionItems({QStringLiteral("print"), QStringLiteral("property")});
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents();
    auto *completer = editor.findChild<QCompleter *>();
    QVERIFY(completer != nullptr);

    QTest::keyClicks(&editor, QStringLiteral("prix"));
    QVERIFY(!editor.completionPopupVisible());
    QTest::keyClick(&editor, Qt::Key_Backspace);
    QVERIFY(editor.completionPopupVisible());
    QCOMPARE(completer->completionCount(), 1);

    QTest::keyClick(&editor, Qt::Key_Backspace);
    QVERIFY(editor.completionPopupVisible());
    QCOMPARE(completer->completionCount(), 2);
}

void EditorTests::escapeThenEnterDoesNotAcceptCompletion()
{
    CodeEditor editor;
    editor.setCompletionItems({QStringLiteral("print"), QStringLiteral("property")});
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents();

    QTest::keyClicks(&editor, QStringLiteral("pr"));
    QVERIFY(editor.completionPopupVisible());
    QTest::keyClick(&editor, Qt::Key_Escape);
    QVERIFY(!editor.completionPopupVisible());

    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.toPlainText(), QStringLiteral("pr\n"));
}

void EditorTests::autoClosesAndKeepsPairsOnBackspace()
{
    CodeEditor editor;
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents();

    QTest::keyClick(&editor, Qt::Key_ParenLeft);
    QCOMPARE(editor.toPlainText(), QStringLiteral("()"));
    QCOMPARE(editor.textCursor().position(), 1);
    QTest::keyClick(&editor, Qt::Key_ParenRight);
    QCOMPARE(editor.toPlainText(), QStringLiteral("()"));
    QCOMPARE(editor.textCursor().position(), 2);

    editor.clear();
    QTest::keyClick(&editor, Qt::Key_BracketLeft);
    QCOMPARE(editor.toPlainText(), QStringLiteral("[]"));

    editor.clear();
    QTest::keyClick(&editor, Qt::Key_ParenLeft);
    QTest::keyClick(&editor, Qt::Key_Backspace);
    QCOMPARE(editor.toPlainText(), QStringLiteral(")"));
    QCOMPARE(editor.textCursor().position(), 0);

    editor.clear();
    QTest::keyClick(&editor, Qt::Key_QuoteDbl);
    QCOMPARE(editor.toPlainText(), QStringLiteral("\"\""));
    QCOMPARE(editor.textCursor().position(), 1);
    QTest::keyClick(&editor, Qt::Key_QuoteDbl);
    QCOMPARE(editor.toPlainText(), QStringLiteral("\"\""));
    QCOMPARE(editor.textCursor().position(), 2);

    editor.setPlainText(QStringLiteral("value"));
    editor.selectAll();
    QTest::keyClick(&editor, Qt::Key_BraceLeft);
    QCOMPARE(editor.toPlainText(), QStringLiteral("{value}"));
    QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("value"));
}

void EditorTests::autoIndentsUsingLanguagePlugins()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pythonPath = directory.filePath(QStringLiteral("indent.py"));
    const QString cppPath = directory.filePath(QStringLiteral("indent.cpp"));
    for (const QString &path : {pythonPath, cppPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    MainWindow window;
    window.loadServicesNow();
    window.openFile(pythonPath);
    auto *pythonEditor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(pythonEditor != nullptr);
    pythonEditor->setPlainText(QStringLiteral("if ready:"));
    pythonEditor->moveCursor(QTextCursor::End);
    QTest::keyClick(pythonEditor, Qt::Key_Return);
    QCOMPARE(pythonEditor->toPlainText(), QStringLiteral("if ready:\n    "));
    QTest::keyClick(pythonEditor, Qt::Key_Backspace);
    QCOMPARE(pythonEditor->toPlainText(), QStringLiteral("if ready:\n"));

    pythonEditor->setPlainText(QStringLiteral("      "));
    pythonEditor->moveCursor(QTextCursor::End);
    QTest::keyClick(pythonEditor, Qt::Key_Backspace);
    QCOMPARE(pythonEditor->toPlainText(), QStringLiteral("    "));

    window.openFile(cppPath);
    auto *cppEditor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(cppEditor != nullptr);
    cppEditor->clear();
    QTest::keyClick(cppEditor, Qt::Key_BraceLeft);
    QCOMPARE(cppEditor->toPlainText(), QStringLiteral("{}"));
    QTest::keyClick(cppEditor, Qt::Key_Return);
    QCOMPARE(cppEditor->toPlainText(), QStringLiteral("{\n    \n}"));
    QCOMPARE(cppEditor->textCursor().position(), 6);
}

void EditorTests::problemsPanelTracksDiagnostics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken.py"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("def broken()\n    pass\n");
    file.close();

    MainWindow window;
    window.loadServicesNow();
    window.openFile(path);
    auto *problems = window.findChild<QTreeWidget *>(QStringLiteral("problemsTree"));
    QVERIFY(problems != nullptr);
    QVERIFY(problems->topLevelItemCount() > 0);
    auto *fileGroup = problems->topLevelItem(0);
    QVERIFY(fileGroup->childCount() > 0);
    auto *severityGroup = fileGroup->child(0);
    QVERIFY(severityGroup->childCount() > 0);
    QVERIFY(severityGroup->child(0)->text(2).contains(QStringLiteral("Expected ':'")));
}

void EditorTests::nativeWorkspaceFeaturesAreLazyAndAvailable()
{
    ApplicationSettingsGuard applicationSettings;
    QSettings().remove(QStringLiteral("session/documents"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("symbols.py"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("class Greeter:\n    def hello(self):\n        return 'hi'\n");
    file.close();

    MainWindow window;
    QVERIFY(window.projectTree()->isHidden());
    QVERIFY(window.projectExplorer()->findChild<QPushButton *>(
        QStringLiteral("explorerOpenFolder")) != nullptr);
    window.loadServicesNow();
    window.openProject(directory.path());
    QVERIFY(!window.projectTree()->isHidden());
    QVERIFY(window.projectTree()->expandsOnDoubleClick());
    QVERIFY(window.projectExplorer()->findChild<QLineEdit *>(
        QStringLiteral("explorerFilter")) != nullptr);

    for (const QString &id : {QStringLiteral("quickOpenAction"),
                              QStringLiteral("projectSearchAction"),
                              QStringLiteral("splitEditorAction"),
                              QStringLiteral("toggleExplorerAction"),
                              QStringLiteral("keybindingsAction"),
                              QStringLiteral("newTerminalAction")})
        QVERIFY2(window.findChild<QAction *>(id) != nullptr, qPrintable(id));
    auto *openFolderAction = window.findChild<QAction *>(QStringLiteral("openProjectAction"));
    QVERIFY(openFolderAction != nullptr);
    QVERIFY(openFolderAction->text().contains(QStringLiteral("Open Folder")));
    QVERIFY(window.findChild<QToolBar *>(QStringLiteral("fileToolbar"))
                ->actions().contains(openFolderAction));
    auto *extensionStoreAction = window.findChild<QAction *>(
        QStringLiteral("extensionStoreAction"));
    QVERIFY(extensionStoreAction != nullptr);
    QCOMPARE(extensionStoreAction->text(), QStringLiteral("Extension Store…"));
    QVERIFY(window.findChild<QToolBar *>(QStringLiteral("fileToolbar"))
                ->actions().contains(extensionStoreAction));
    KeybindingsDialog keybindings(window.findChildren<QAction *>(), &window);
    auto *keybindingsTable = keybindings.findChild<QTableWidget *>(QStringLiteral("keybindingsTable"));
    QVERIFY(keybindingsTable != nullptr);
    QCOMPARE(keybindingsTable->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOn);

    window.openFile(path);
    const auto symbols = window.languageManager()->documentSymbols(
        path, QStringLiteral("class Greeter:\n    def hello(self):\n        pass\n"));
    QCOMPARE(symbols.size(), 2);
    auto *outline = window.findChild<QTreeWidget *>(QStringLiteral("outlineTree"));
    QVERIFY(outline != nullptr);
    QVERIFY(outline->topLevelItemCount() > 0);

    window.findChild<QAction *>(QStringLiteral("splitEditorAction"))->trigger();
    auto *secondaryTabs = window.findChild<QTabWidget *>(QStringLiteral("secondaryEditorTabs"));
    QVERIFY(secondaryTabs != nullptr);
    auto *tabMime = new QMimeData;
    tabMime->setData(QStringLiteral("application/x-native-studio-editor-tab"), path.toUtf8());
    QDragEnterEvent tabDragEnter(QPoint(20, 20), Qt::CopyAction, tabMime,
                                 Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.editorTabs(), &tabDragEnter);
    QDropEvent tabDrop(QPointF(20, 20), Qt::CopyAction, tabMime,
                       Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.editorTabs(), &tabDrop);
    bool movedToPrimary = false;
    for (int index = 0; index < window.editorTabs()->count(); ++index) {
        auto *candidate = qobject_cast<CodeEditor *>(window.editorTabs()->widget(index));
        if (candidate && candidate->filePath() == QFileInfo(path).absoluteFilePath()) {
            movedToPrimary = true;
            break;
        }
    }
    QVERIFY(movedToPrimary);
    delete tabMime;
    auto *terminals = window.findChild<QTabWidget *>(QStringLiteral("terminalTabs"));
    QVERIFY(terminals != nullptr);
    const int terminalCount = terminals->count();
    window.findChild<QAction *>(QStringLiteral("newTerminalAction"))->trigger();
    QCOMPARE(terminals->count(), terminalCount + 1);

    window.findChild<QAction *>(QStringLiteral("projectSearchAction"))->trigger();
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("projectSearchWidget")) != nullptr);
    auto *gitAction = window.findChild<QAction *>(QStringLiteral(
        "extensionCommand.dev.nativestudio.git.open_source_control"));
    QVERIFY(gitAction != nullptr);
    QCOMPARE(gitAction->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+G")));
    gitAction->trigger();
    auto *gitWidget = window.findChild<QWidget *>(QStringLiteral("gitWidget"));
    QVERIFY(gitWidget != nullptr);
    for (const QString &button : {QStringLiteral("gitPull"), QStringLiteral("gitPush"),
                                  QStringLiteral("gitRestore"), QStringLiteral("gitStageAll"),
                                  QStringLiteral("gitUnstageAll"), QStringLiteral("gitSwitchBranch"),
                                  QStringLiteral("gitCommit"), QStringLiteral("gitMoreActions")}) {
        auto *toolButton = gitWidget->findChild<QToolButton *>(button);
        QVERIFY2(toolButton != nullptr, qPrintable(button));
        QVERIFY2(toolButton->text().isEmpty(), qPrintable(button));
        QVERIFY2(toolButton->width() <= 28, qPrintable(button));
    }
    for (const QString &action : {QStringLiteral("githubSignIn"), QStringLiteral("gitFetch"),
                                  QStringLiteral("gitHistory"), QStringLiteral("gitStash"),
                                  QStringLiteral("gitStashPop"), QStringLiteral("gitCreateBranch"),
                                  QStringLiteral("gitInit"), QStringLiteral("gitClone"),
                                  QStringLiteral("gitOpenGitHub")})
        QVERIFY2(gitWidget->findChild<QAction *>(action) != nullptr, qPrintable(action));
    for (const QString &command : {
             QStringLiteral("dev.nativestudio.git.sign_in_github"),
             QStringLiteral("dev.nativestudio.git.pull"),
             QStringLiteral("dev.nativestudio.git.push")}) {
        QVERIFY(window.extensionManager()->commandIds().contains(command));
    }

    const auto extensions = window.extensionManager()->installedExtensions();
    const auto tidy = std::find_if(extensions.cbegin(), extensions.cend(), [](const auto &extension) {
        return extension.id == QStringLiteral("dev.nativestudio.code_tidy");
    });
    QVERIFY(tidy != extensions.cend());
    QVERIFY(tidy->permissions.contains(QStringLiteral("editor.write")));
    const auto git = std::find_if(extensions.cbegin(), extensions.cend(), [](const auto &extension) {
        return extension.id == QStringLiteral("dev.nativestudio.git");
    });
    QVERIFY(git != extensions.cend());
    QVERIFY(git->bundled);
    QVERIFY(git->permissions.contains(QStringLiteral("ui.views")));
    QVERIFY(git->permissions.contains(QStringLiteral("process.execute")));
    QVERIFY(window.extensionManager()->viewIds().contains(QStringLiteral("git.sourceControl")));

    auto *secondary = window.findChild<QTabWidget *>(QStringLiteral("secondaryEditorTabs"));
    auto *editor = qobject_cast<CodeEditor *>(secondary ? secondary->currentWidget() : nullptr);
    if (!editor) editor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(editor != nullptr);
    editor->appendPlainText(QStringLiteral("# unsaved session text"));
    QVERIFY(window.close());
    const QVariantList session = QSettings().value(QStringLiteral("session/documents")).toList();
    QVERIFY(!session.isEmpty());
    QVERIFY(std::any_of(session.cbegin(), session.cend(), [](const QVariant &entry) {
        return entry.toMap().value(QStringLiteral("text")).toString()
            .contains(QStringLiteral("unsaved session text"));
    }));

    MainWindow restored;
    restored.restoreSessionDeferred();
    QTRY_VERIFY_WITH_TIMEOUT(restored.findChild<QTabWidget *>(
        QStringLiteral("secondaryEditorTabs")) != nullptr, 2000);
    bool restoredUnsavedText = false;
    for (QTabWidget *tabs : restored.findChildren<QTabWidget *>()) {
        for (int index = 0; index < tabs->count(); ++index) {
            auto *restoredEditor = qobject_cast<CodeEditor *>(tabs->widget(index));
            restoredUnsavedText = restoredUnsavedText || (restoredEditor
                && restoredEditor->toPlainText().contains(QStringLiteral("unsaved session text")));
        }
    }
    QVERIFY(restoredUnsavedText);
}

void EditorTests::findAndReplaceCurrentDocument()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    auto *editor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(editor != nullptr);
    editor->setPlainText(QStringLiteral("one two one"));
    editor->moveCursor(QTextCursor::Start);
    window.findChild<QAction *>(QStringLiteral("replaceAction"))->trigger();
    auto *popup = window.findChild<QWidget *>(QStringLiteral("findReplaceBar"));
    auto *replaceRow = window.findChild<QWidget *>(QStringLiteral("replaceText"))->parentWidget();
    QVERIFY(popup != nullptr);
    QVERIFY(!popup->isHidden());
    QVERIFY(!replaceRow->isHidden());
    QVERIFY(popup->geometry().right() <= window.editorTabs()->geometry().right());
    auto *find = window.findChild<QLineEdit *>(QStringLiteral("findText"));
    auto *replace = window.findChild<QLineEdit *>(QStringLiteral("replaceText"));
    QVERIFY(find != nullptr);
    QVERIFY(replace != nullptr);
    find->setText(QStringLiteral("one"));
    replace->setText(QStringLiteral("three"));
    window.findChild<QAction *>(QStringLiteral("findNextButton"))->trigger();
    QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("one"));
    window.findChild<QAction *>(QStringLiteral("replaceCurrentButton"))->trigger();
    window.findChild<QAction *>(QStringLiteral("replaceAllButton"))->trigger();
    QCOMPARE(editor->toPlainText(), QStringLiteral("three two three"));
    QTest::keyClick(find, Qt::Key_Escape);
    QVERIFY(popup->isHidden());
}

void EditorTests::closingLastFileShowsEmptyEditorState()
{
    ApplicationSettingsGuard applicationSettings;
    MainWindow window;
    auto *closeButton = qobject_cast<QToolButton *>(
        window.editorTabs()->tabBar()->tabButton(0, QTabBar::RightSide));
    QVERIFY(closeButton != nullptr);
    QTest::mouseClick(closeButton, Qt::LeftButton);
    QCOMPARE(window.editorTabs()->count(), 0);
    auto *empty = window.findChild<QWidget *>(QStringLiteral("editorEmptyState"));
    QVERIFY(empty != nullptr);
    QVERIFY(!empty->isHidden());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    window.openProject(directory.path());
    auto *closeFolder = window.findChild<QAction *>(QStringLiteral("closeProjectAction"));
    QVERIFY(closeFolder != nullptr);
    QCOMPARE(closeFolder->text(), QStringLiteral("Close Folder"));
    QVERIFY(closeFolder->icon().isNull());
    QVERIFY(window.findChild<QToolBar *>(QStringLiteral("fileToolbar"))
                ->actions().contains(closeFolder));
    QVERIFY(closeFolder->isEnabled());
    closeFolder->trigger();
    QVERIFY(window.projectDirectory().isEmpty());
    QVERIFY(window.projectTree()->isHidden());
    QVERIFY(!closeFolder->isEnabled());
    QCOMPARE(window.editorTabs()->count(), 0);
}

void EditorTests::autoSaveWritesNamedFiles()
{
    ApplicationSettingsGuard applicationSettings;
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
    QSettings().setValue(QStringLiteral("editor/autoSaveDelayMs"), 250);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("autosave.txt"));
    QFile seed(path);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write("before");
    seed.close();
    MainWindow window;
    window.loadServicesNow();
    window.openFile(path);
    auto *editor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(editor != nullptr);
    QTextCursor cursor = editor->textCursor();
    cursor.select(QTextCursor::Document);
    cursor.insertText(QStringLiteral("after"));
    editor->setTextCursor(cursor);
    QVERIFY(editor->document()->isModified());
    QTRY_VERIFY_WITH_TIMEOUT(!editor->document()->isModified(), 3000);
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(saved.readAll()).trimmed(), QStringLiteral("after"));
}

void EditorTests::restoresExtensionDockLayoutAfterDeferredLoad()
{
    ApplicationSettingsGuard applicationSettings;
    QSettings().clear();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    {
        MainWindow original;
        original.show();
        original.openProject(directory.path());
        original.loadServicesNow();
        QVERIFY(original.showExtensionView(QStringLiteral("git.sourceControl")));
        auto *gitDock = original.findChild<QDockWidget *>(QStringLiteral("gitDock"));
        auto *outlineDock = original.findChild<QDockWidget *>(QStringLiteral("outlineDock"));
        QVERIFY(gitDock != nullptr);
        QVERIFY(outlineDock != nullptr);
        outlineDock->show();
        gitDock->show();
        original.addDockWidget(Qt::RightDockWidgetArea, gitDock);
        QCoreApplication::processEvents();
        original.tabifyDockWidget(outlineDock, gitDock);
        gitDock->raise();
        QCoreApplication::processEvents();
        QVERIFY(original.tabifiedDockWidgets(outlineDock).contains(gitDock));
        QVERIFY(original.close());
    }

    MainWindow restored;
    QVERIFY(restored.restoreWindowLayout());
    QVERIFY(restored.findChild<QDockWidget *>(QStringLiteral("gitDock")) == nullptr);
    restored.loadServicesNow();
    auto *gitDock = restored.findChild<QDockWidget *>(QStringLiteral("gitDock"));
    auto *outlineDock = restored.findChild<QDockWidget *>(QStringLiteral("outlineDock"));
    QVERIFY(gitDock != nullptr);
    QVERIFY(outlineDock != nullptr);
    QCOMPARE(restored.dockWidgetArea(gitDock), Qt::RightDockWidgetArea);
    QVERIFY(restored.tabifiedDockWidgets(outlineDock).contains(gitDock));
    QCOMPARE(gitDock->property("extensionViewId").toString(),
             QStringLiteral("git.sourceControl"));
    QSettings().clear();
}

void EditorTests::gitChangesViewGroupsRepositoryState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto git = [&directory](const QStringList &arguments) {
        QProcess process;
        process.setWorkingDirectory(directory.path());
        process.start(QStringLiteral("git"), arguments);
        return process.waitForFinished(5000) && process.exitCode() == 0;
    };
    if (!git({QStringLiteral("init")})) QSKIP("Git is unavailable");
    QVERIFY(git({QStringLiteral("config"), QStringLiteral("user.email"),
                 QStringLiteral("tests@nativestudio.local")}));
    QVERIFY(git({QStringLiteral("config"), QStringLiteral("user.name"),
                 QStringLiteral("Native Studio Tests")}));
    QFile tracked(directory.filePath(QStringLiteral("tracked.cpp")));
    QVERIFY(tracked.open(QIODevice::WriteOnly));
    tracked.write("int value = 1;\n");
    tracked.close();
    QVERIFY(git({QStringLiteral("add"), QStringLiteral("tracked.cpp")}));
    QVERIFY(git({QStringLiteral("commit"), QStringLiteral("-m"), QStringLiteral("initial")}));
    QVERIFY(tracked.open(QIODevice::Append));
    tracked.write("int changed = 2;\n");
    tracked.close();
    QFile staged(directory.filePath(QStringLiteral("staged.py")));
    QVERIFY(staged.open(QIODevice::WriteOnly));
    staged.write("print('staged')\n");
    staged.close();
    QVERIFY(git({QStringLiteral("add"), QStringLiteral("staged.py")}));
    QFile untracked(directory.filePath(QStringLiteral("untracked.txt")));
    QVERIFY(untracked.open(QIODevice::WriteOnly));
    untracked.write("new\n");
    untracked.close();

    MainWindow window;
    window.openProject(directory.path());
    window.loadServicesNow();
    QVERIFY(window.showExtensionView(QStringLiteral("git.sourceControl")));
    auto *changes = window.findChild<QTreeWidget *>(QStringLiteral("gitChanges"));
    QVERIFY(changes != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(changes->topLevelItemCount(), 3, 5000);
    QStringList groups;
    QStringList files;
    for (int row = 0; row < changes->topLevelItemCount(); ++row) {
        QTreeWidgetItem *group = changes->topLevelItem(row);
        groups.push_back(group->text(0));
        for (int child = 0; child < group->childCount(); ++child)
            files.push_back(group->child(child)->text(0));
    }
    QVERIFY(std::any_of(groups.cbegin(), groups.cend(), [](const QString &group) {
        return group.startsWith(QStringLiteral("STAGED CHANGES"));
    }));
    QVERIFY(std::any_of(groups.cbegin(), groups.cend(), [](const QString &group) {
        return group.startsWith(QStringLiteral("CHANGES"));
    }));
    QVERIFY(std::any_of(groups.cbegin(), groups.cend(), [](const QString &group) {
        return group.startsWith(QStringLiteral("UNTRACKED"));
    }));
    QVERIFY(files.contains(QStringLiteral("staged.py")));
    QVERIFY(files.contains(QStringLiteral("tracked.cpp")));
    QVERIFY(files.contains(QStringLiteral("untracked.txt")));
    const QModelIndex trackedIndex = window.projectExplorer()->model()->index(
        directory.filePath(QStringLiteral("tracked.cpp")));
    QVERIFY(trackedIndex.isValid());
    QVERIFY(trackedIndex.data(Qt::ForegroundRole).isValid());
    QVERIFY(trackedIndex.data(Qt::ToolTipRole).toString().contains(
        QStringLiteral("Modified"), Qt::CaseInsensitive));
}

QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
