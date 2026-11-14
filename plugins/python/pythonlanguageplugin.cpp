#include "pythonlanguageplugin.h"

#include "pythonhighlighter.h"

#include <QFileInfo>

QString PythonLanguagePlugin::languageName() const
{
    return QStringLiteral("Python");
}

QStringList PythonLanguagePlugin::fileExtensions() const
{
    return {QStringLiteral("py"), QStringLiteral("pyw"), QStringLiteral("pyi")};
}

QSyntaxHighlighter *PythonLanguagePlugin::createHighlighter(QTextDocument *document)
{
    return new PythonHighlighter(document);
}

RunConfiguration PythonLanguagePlugin::runConfiguration(const QString &filePath) const
{
    RunConfiguration configuration;
#ifdef Q_OS_WIN
    configuration.program = QStringLiteral("py");
    configuration.arguments = {QStringLiteral("-3"), QFileInfo(filePath).absoluteFilePath()};
#else
    configuration.program = QStringLiteral("python3");
    configuration.arguments = {QFileInfo(filePath).absoluteFilePath()};
#endif
    configuration.workingDirectory = QFileInfo(filePath).absolutePath();
    return configuration;
}
