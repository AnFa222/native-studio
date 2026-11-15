#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QtPlugin>

class QSyntaxHighlighter;
class QTextDocument;
class QWidget;

struct RunConfiguration
{
    QString program;
    QStringList arguments;
    QString workingDirectory;

    [[nodiscard]] bool isValid() const { return !program.isEmpty(); }
};

enum class DiagnosticSeverity
{
    Warning,
    Error
};

struct CodeDiagnostic
{
    int line = 0;
    int column = 0;
    int length = 1;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    QString message;
};

class LanguagePlugin
{
public:
    virtual ~LanguagePlugin() = default;

    virtual void shutdown() {}

    [[nodiscard]] virtual QString languageName() const = 0;
    [[nodiscard]] virtual QStringList fileExtensions() const = 0;
    virtual QSyntaxHighlighter *createHighlighter(QTextDocument *document) = 0;
    [[nodiscard]] virtual QStringList completionItems(const QString &documentText) const
    {
        Q_UNUSED(documentText)
        return {};
    }
    [[nodiscard]] virtual QList<CodeDiagnostic> diagnostics(const QString &documentText) const
    {
        Q_UNUSED(documentText)
        return {};
    }
    [[nodiscard]] virtual RunConfiguration runConfiguration(const QString &filePath) const
    {
        Q_UNUSED(filePath)
        return {};
    }
};

#define LanguagePlugin_iid "dev.nativestudio.LanguagePlugin/3.0"
Q_DECLARE_INTERFACE(LanguagePlugin, LanguagePlugin_iid)

class LanguageStatusProvider
{
public:
    virtual ~LanguageStatusProvider() = default;
    virtual QWidget *createStatusWidget(QWidget *parent) = 0;
};

#define LanguageStatusProvider_iid "dev.nativestudio.LanguageStatusProvider/1.0"
Q_DECLARE_INTERFACE(LanguageStatusProvider, LanguageStatusProvider_iid)
