#include "codeeditor.h"
#include "extensionmanager.h"
#include "extensionmanagerdialog.h"
#include "languagemanager.h"
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
#include <QTabWidget>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QStatusBar>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QTreeWidget>
#include <QToolButton>

#include <algorithm>

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
    void extensionManagerShowsManifestControls();
    void settingsCenterIncludesCoreAndPluginPages();
    void languagePluginsProvideCompletions();
    void languagePluginsDetectErrors();
    void pythonAnalyzerFindsSemanticProblems();
    void languagePluginsAcceptValidSyntax();
    void editorAcceptsPopupCompletion();
    void analysisRefreshKeepsCompletionOpen();
    void backspaceRestoresCompletionSuggestions();
    void escapeThenEnterDoesNotAcceptCompletion();
    void autoClosesAndKeepsPairsOnBackspace();
    void problemsPanelTracksDiagnostics();
};

void EditorTests::startsWithOneUntitledEditor()
{
    MainWindow window;
    QCOMPARE(window.editorTabs()->count(), 1);
    QCOMPARE(window.editorTabs()->tabText(0), QStringLiteral("Untitled"));
    QVERIFY(window.projectTree() != nullptr);
    QVERIFY(window.languageManager()->languages().isEmpty());
    QVERIFY(window.extensionManager()->loadedExtensions().isEmpty());
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
    auto *terminalView = window.terminal()->findChild<QPlainTextEdit *>(
        QStringLiteral("terminalOutput"));
    auto *terminate = window.terminal()->findChild<QToolButton *>(
        QStringLiteral("terminateTerminalButton"));
    QVERIFY(terminalView != nullptr);
    QVERIFY(terminalView->styleSheet().contains(QStringLiteral("#ffffff")));
    QVERIFY(terminate != nullptr);
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
    auto *terminate = window.terminal()->findChild<QToolButton *>(
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
    QVERIFY(search != nullptr);
    QVERIFY(table != nullptr);
    QVERIFY(import != nullptr);
    QVERIFY(toggle != nullptr);
    QVERIFY(remove != nullptr);
    QCOMPARE(table->columnCount(), 6);
    QVERIFY(table->rowCount() >= 1);
}

void EditorTests::settingsCenterIncludesCoreAndPluginPages()
{
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
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("settingsManagePlugins")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("settingsManageExtensions")) != nullptr);
    QVERIFY(dialog.findChild<QWidget *>(QStringLiteral("pythonSettingsPage")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("selectPythonInterpreter")) != nullptr);

    search->setText(QStringLiteral("interpreter"));
    int visibleCategories = 0;
    for (int index = 0; index < categories->count(); ++index)
        visibleCategories += categories->item(index)->isHidden() ? 0 : 1;
    QCOMPARE(visibleCategories, 1);
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
    QVERIFY(problems->topLevelItem(0)->text(2).contains(QStringLiteral("Expected ':'")));
}

QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
