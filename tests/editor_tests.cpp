#include "codeeditor.h"
#include "languagemanager.h"
#include "mainwindow.h"
#include "pluginmanagerdialog.h"
#include "projectexplorer.h"
#include "terminalwidget.h"

#include <QFile>
#include <QAction>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QTreeWidget>

#include <algorithm>

class EditorTests final : public QObject
{
    Q_OBJECT

private slots:
    void startsWithOneUntitledEditor();
    void roundTripsUtf8File();
    void openingSameFileReusesTab();
    void highlightsCppKeywords();
    void explorerShowsEveryFileType();
    void loadsPythonPlugin();
    void unknownLanguageUsesPlainText();
    void terminalRunsProgram();
    void runActionTracksSupportedFile();
    void lineNumberGutterTracksDocument();
    void pluginExplorerHasMarketplaceControls();
    void languagePluginsProvideCompletions();
    void languagePluginsDetectErrors();
    void editorAcceptsInlineCompletion();
    void analysisRefreshKeepsCompletionOpen();
    void problemsPanelTracksDiagnostics();
};

void EditorTests::startsWithOneUntitledEditor()
{
    MainWindow window;
    QCOMPARE(window.editorTabs()->count(), 1);
    QCOMPARE(window.editorTabs()->tabText(0), QStringLiteral("Untitled"));
    QVERIFY(window.projectTree() != nullptr);
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
    QVERIFY(window.languageManager()->languages().contains(QStringLiteral("C++")));
    window.openFile(path);
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
    QVERIFY(window.languageManager()->languages().contains(QStringLiteral("Python")));
    QCOMPARE(window.languageManager()->languageForFile(path), QStringLiteral("Python"));
    const auto runConfiguration = window.languageManager()->runConfiguration(path);
    QVERIFY(runConfiguration.isValid());
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

void EditorTests::languagePluginsProvideCompletions()
{
    MainWindow window;
    const auto pythonItems = window.languageManager()->completionItems(
        QStringLiteral("example.py"), QStringLiteral("def custom_symbol():\n    pri"));
    QVERIFY(pythonItems.contains(QStringLiteral("print")));
    QVERIFY(pythonItems.contains(QStringLiteral("custom_symbol")));

    const auto cppItems = window.languageManager()->completionItems(
        QStringLiteral("example.cpp"), QStringLiteral("struct CustomWidget {};\nconste"));
    QVERIFY(cppItems.contains(QStringLiteral("constexpr")));
    QVERIFY(cppItems.contains(QStringLiteral("CustomWidget")));
}

void EditorTests::languagePluginsDetectErrors()
{
    MainWindow window;
    const auto pythonProblems = window.languageManager()->diagnostics(
        QStringLiteral("broken.py"), QStringLiteral("def greet()\n    print('hello')\n"));
    QVERIFY(!pythonProblems.isEmpty());
    QVERIFY(std::any_of(pythonProblems.cbegin(), pythonProblems.cend(), [](const auto &problem) {
        return problem.message.contains(QStringLiteral("Expected ':'"));
    }));

    const auto cppProblems = window.languageManager()->diagnostics(
        QStringLiteral("broken.cpp"), QStringLiteral("int main() {\n    return 0;\n"));
    QVERIFY(!cppProblems.isEmpty());
    QVERIFY(std::any_of(cppProblems.cbegin(), cppProblems.cend(), [](const auto &problem) {
        return problem.message.contains(QStringLiteral("Unclosed"));
    }));
}

void EditorTests::editorAcceptsInlineCompletion()
{
    CodeEditor editor;
    editor.setCompletionItems({QStringLiteral("print"), QStringLiteral("property")});
    editor.show();
    editor.setFocus();
    QCoreApplication::processEvents();

    QTest::keyClicks(&editor, QStringLiteral("p"));
    QVERIFY(!editor.inlineCompletion().isEmpty());
    QVERIFY(editor.completionPopupVisible());
    QTest::keyClicks(&editor, QStringLiteral("r"));
    QCOMPARE(editor.inlineCompletion(), QStringLiteral("int"));
    QVERIFY(editor.completionPopupVisible());

    // Simulate the delayed plugin-analysis pass. It discovers the partial
    // document identifier "pr", but must not collapse the active popup.
    editor.setCompletionItems({QStringLiteral("pr"), QStringLiteral("print"),
                               QStringLiteral("property")});
    QCOMPARE(editor.inlineCompletion(), QStringLiteral("int"));
    QVERIFY(editor.completionPopupVisible());

    QTest::keyClicks(&editor, QStringLiteral("i"));
    QCOMPARE(editor.inlineCompletion(), QStringLiteral("nt"));
    QVERIFY(editor.completionPopupVisible());
    QTest::keyClicks(&editor, QStringLiteral("n"));
    QCOMPARE(editor.inlineCompletion(), QStringLiteral("t"));
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
    window.openFile(path);
    window.show();
    auto *editor = qobject_cast<CodeEditor *>(window.editorTabs()->currentWidget());
    QVERIFY(editor != nullptr);
    editor->setFocus();
    QCoreApplication::processEvents();

    QTest::keyClicks(editor, QStringLiteral("pr"));
    QTRY_VERIFY(editor->completionPopupVisible());
    QCOMPARE(editor->inlineCompletion(), QStringLiteral("int"));
    QTest::qWait(350);
    QVERIFY(editor->completionPopupVisible());
    QCOMPARE(editor->inlineCompletion(), QStringLiteral("int"));

    QTest::keyClicks(editor, QStringLiteral("i"));
    QTest::qWait(350);
    QVERIFY(editor->completionPopupVisible());
    QCOMPARE(editor->inlineCompletion(), QStringLiteral("nt"));
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
    window.openFile(path);
    auto *problems = window.findChild<QTreeWidget *>(QStringLiteral("problemsTree"));
    QVERIFY(problems != nullptr);
    QVERIFY(problems->topLevelItemCount() > 0);
    QVERIFY(problems->topLevelItem(0)->text(2).contains(QStringLiteral("Expected ':'")));
}

QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
