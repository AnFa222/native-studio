#include "cpplanguageplugin.h"

#include "cpphighlighter.h"

QString CppLanguagePlugin::languageName() const
{
    return QStringLiteral("C++");
}

QStringList CppLanguagePlugin::fileExtensions() const
{
    return {QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
            QStringLiteral("cxx"), QStringLiteral("h"), QStringLiteral("hh"),
            QStringLiteral("hpp"), QStringLiteral("hxx"), QStringLiteral("ixx"),
            QStringLiteral("cppm")};
}

QSyntaxHighlighter *CppLanguagePlugin::createHighlighter(QTextDocument *document)
{
    return new CppHighlighter(document);
}
