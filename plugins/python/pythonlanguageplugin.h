#pragma once

#include "languageplugin.h"

#include <QObject>

#include <optional>

class QProcess;

class PythonLanguagePlugin final : public QObject, public LanguagePlugin, public LanguageStatusProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LanguagePlugin_iid FILE "pythonplugin.json")
    Q_INTERFACES(LanguagePlugin LanguageStatusProvider)

public:
    ~PythonLanguagePlugin() override;
    void shutdown() override;
    [[nodiscard]] QString languageName() const override;
    [[nodiscard]] QStringList fileExtensions() const override;
    QSyntaxHighlighter *createHighlighter(QTextDocument *document) override;
    [[nodiscard]] RunConfiguration runConfiguration(const QString &filePath) const override;
    [[nodiscard]] QStringList completionItems(const QString &documentText) const override;
    [[nodiscard]] QList<CodeDiagnostic> diagnostics(const QString &documentText) const override;
    QWidget *createStatusWidget(QWidget *parent) override;

private:
    [[nodiscard]] QString interpreterExecutable() const;
    [[nodiscard]] QStringList interpreterPrefixArguments(const QString &executable) const;
    [[nodiscard]] bool ensureAnalyzerRunning() const;
    [[nodiscard]] std::optional<QList<CodeDiagnostic>>
        analyzerDiagnostics(const QString &documentText) const;

    mutable QProcess *m_analyzerProcess = nullptr;
    mutable bool m_analyzerUnavailable = false;
};
