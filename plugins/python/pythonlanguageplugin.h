#pragma once

#include "languageplugin.h"

#include <QObject>

#include <optional>

class QProcess;

class PythonLanguagePlugin final : public QObject, public LanguagePlugin,
                                   public LanguageStatusProvider, public LanguageSettingsProvider,
                                   public LanguageIndentationProvider, public LanguageSymbolProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LanguagePlugin_iid FILE "pythonplugin.json")
    Q_INTERFACES(LanguagePlugin LanguageStatusProvider LanguageSettingsProvider
                 LanguageIndentationProvider LanguageSymbolProvider)

public:
    ~PythonLanguagePlugin() override;
    void shutdown() override;
    [[nodiscard]] QString languageName() const override;
    [[nodiscard]] QStringList fileExtensions() const override;
    QSyntaxHighlighter *createHighlighter(QTextDocument *document) override;
    [[nodiscard]] RunConfiguration runConfiguration(const QString &filePath) const override;
    [[nodiscard]] QStringList completionItems(const QString &documentText) const override;
    [[nodiscard]] QList<CodeDiagnostic> diagnostics(const QString &documentText) const override;
    [[nodiscard]] int indentationDeltaAfterLine(const QString &lineText) const override;
    [[nodiscard]] QList<CodeSymbol> documentSymbols(const QString &documentText) const override;
    QWidget *createStatusWidget(QWidget *parent) override;
    [[nodiscard]] QString settingsTitle() const override;
    [[nodiscard]] QStringList settingsKeywords() const override;
    QWidget *createSettingsWidget(QWidget *parent) override;

signals:
    void interpreterChanged();

private:
    bool selectInterpreter(QWidget *parent);
    void useAutomaticInterpreter();
    [[nodiscard]] QString interpreterExecutable() const;
    [[nodiscard]] QStringList interpreterPrefixArguments(const QString &executable) const;
    [[nodiscard]] bool ensureAnalyzerRunning() const;
    [[nodiscard]] std::optional<QList<CodeDiagnostic>>
        analyzerDiagnostics(const QString &documentText) const;

    mutable QProcess *m_analyzerProcess = nullptr;
    mutable bool m_analyzerUnavailable = false;
};
