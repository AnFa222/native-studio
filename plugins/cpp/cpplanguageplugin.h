#pragma once

#include "languageplugin.h"

#include <QObject>

class CppLanguagePlugin final : public QObject, public LanguagePlugin,
                                public LanguageIndentationProvider
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LanguagePlugin_iid FILE "cppplugin.json")
    Q_INTERFACES(LanguagePlugin LanguageIndentationProvider)

public:
    [[nodiscard]] QString languageName() const override;
    [[nodiscard]] QStringList fileExtensions() const override;
    QSyntaxHighlighter *createHighlighter(QTextDocument *document) override;
    [[nodiscard]] QStringList completionItems(const QString &documentText) const override;
    [[nodiscard]] QList<CodeDiagnostic> diagnostics(const QString &documentText) const override;
    [[nodiscard]] int indentationDeltaAfterLine(const QString &lineText) const override;
};
