#pragma once

#include "languageplugin.h"

#include <QObject>

class PythonLanguagePlugin final : public QObject, public LanguagePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LanguagePlugin_iid FILE "pythonplugin.json")
    Q_INTERFACES(LanguagePlugin)

public:
    [[nodiscard]] QString languageName() const override;
    [[nodiscard]] QStringList fileExtensions() const override;
    QSyntaxHighlighter *createHighlighter(QTextDocument *document) override;
    [[nodiscard]] RunConfiguration runConfiguration(const QString &filePath) const override;
    [[nodiscard]] QStringList completionItems(const QString &documentText) const override;
    [[nodiscard]] QList<CodeDiagnostic> diagnostics(const QString &documentText) const override;
};
