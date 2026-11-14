#pragma once

#include <QString>
#include <QStringList>
#include <QtPlugin>

class QSyntaxHighlighter;
class QTextDocument;

struct RunConfiguration
{
    QString program;
    QStringList arguments;
    QString workingDirectory;

    [[nodiscard]] bool isValid() const { return !program.isEmpty(); }
};

class LanguagePlugin
{
public:
    virtual ~LanguagePlugin() = default;

    [[nodiscard]] virtual QString languageName() const = 0;
    [[nodiscard]] virtual QStringList fileExtensions() const = 0;
    virtual QSyntaxHighlighter *createHighlighter(QTextDocument *document) = 0;
    [[nodiscard]] virtual RunConfiguration runConfiguration(const QString &filePath) const
    {
        Q_UNUSED(filePath)
        return {};
    }
};

#define LanguagePlugin_iid "dev.nativestudio.LanguagePlugin/1.0"
Q_DECLARE_INTERFACE(LanguagePlugin, LanguagePlugin_iid)
