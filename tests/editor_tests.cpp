#include "codeeditor.h"
#include "languagemanager.h"
#include "mainwindow.h"
#include "projectexplorer.h"
#include "terminalwidget.h"

#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>

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

QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
