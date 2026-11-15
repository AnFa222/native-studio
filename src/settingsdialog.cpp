#include "settingsdialog.h"

#include "extensionmanager.h"
#include "extensionmanagerdialog.h"
#include "languagemanager.h"
#include "pluginmanagerdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
QLabel *heading(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("settingsHeading", true);
    return label;
}
}

SettingsDialog::SettingsDialog(LanguageManager *languages, ExtensionManager *extensions,
                               QWidget *parent)
    : QDialog(parent), m_languages(languages), m_extensions(extensions)
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Settings"));
    resize(920, 620);
    QSettings settings;
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    auto *root = new QVBoxLayout(this);
    auto *search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("settingsSearch"));
    search->setPlaceholderText(tr("Search settings…"));
    root->addWidget(search);
    auto *content = new QHBoxLayout;
    m_categories = new QListWidget(this);
    m_categories->setObjectName(QStringLiteral("settingsCategories"));
    m_categories->setFixedWidth(190);
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName(QStringLiteral("settingsPages"));
    content->addWidget(m_categories);
    content->addWidget(m_pages, 1);
    root->addLayout(content, 1);

    auto *appearancePage = new QWidget(m_pages);
    auto *appearanceLayout = new QVBoxLayout(appearancePage);
    appearanceLayout->addWidget(heading(tr("Appearance"), appearancePage));
    auto *appearanceForm = new QFormLayout;
    m_theme = new QComboBox(appearancePage);
    m_theme->setObjectName(QStringLiteral("colorThemeSetting"));
    m_theme->addItem(tr("Dark"), QStringLiteral("dark"));
    m_theme->addItem(tr("Light"), QStringLiteral("light"));
    m_theme->setCurrentIndex(qMax(0, m_theme->findData(
        settings.value(QStringLiteral("appearance/theme"), QStringLiteral("dark")))));
    m_uiFontSize = new QSpinBox(appearancePage);
    m_uiFontSize->setObjectName(QStringLiteral("uiFontSizeSetting"));
    m_uiFontSize->setRange(8, 20);
    m_uiFontSize->setSuffix(tr(" pt"));
    m_uiFontSize->setValue(settings.value(QStringLiteral("appearance/uiFontSize"),
                                           QApplication::font().pointSize()).toInt());
    appearanceForm->addRow(tr("Color theme:"), m_theme);
    appearanceForm->addRow(tr("Interface font size:"), m_uiFontSize);
    appearanceLayout->addLayout(appearanceForm);
    appearanceLayout->addStretch();
    addPage(tr("Appearance"), {tr("theme"), tr("color"), tr("font"), tr("interface")},
            appearancePage);

    auto *editorPage = new QWidget(m_pages);
    auto *editorLayout = new QVBoxLayout(editorPage);
    editorLayout->addWidget(heading(tr("Text Editor"), editorPage));
    auto *editorForm = new QFormLayout;
    m_editorFont = new QFontComboBox(editorPage);
    m_editorFont->setObjectName(QStringLiteral("editorFontSetting"));
    m_editorFont->setCurrentFont(QFont(settings.value(QStringLiteral("editor/fontFamily"),
                                              fixedFont.family()).toString()));
    m_editorFontSize = new QSpinBox(editorPage);
    m_editorFontSize->setObjectName(QStringLiteral("editorFontSizeSetting"));
    m_editorFontSize->setRange(8, 32);
    m_editorFontSize->setSuffix(tr(" pt"));
    m_editorFontSize->setValue(settings.value(QStringLiteral("editor/fontSize"), 11).toInt());
    m_tabSize = new QSpinBox(editorPage);
    m_tabSize->setObjectName(QStringLiteral("editorTabSizeSetting"));
    m_tabSize->setRange(1, 16);
    m_tabSize->setValue(settings.value(QStringLiteral("editor/tabSize"), 4).toInt());
    m_wordWrap = new QCheckBox(tr("Wrap long lines"), editorPage);
    m_wordWrap->setObjectName(QStringLiteral("editorWordWrapSetting"));
    m_wordWrap->setChecked(settings.value(QStringLiteral("editor/wordWrap"), false).toBool());
    m_autoClosePairs = new QCheckBox(tr("Auto-close brackets and quotes"), editorPage);
    m_autoClosePairs->setObjectName(QStringLiteral("editorAutoClosePairsSetting"));
    m_autoClosePairs->setChecked(settings.value(QStringLiteral("editor/autoClosePairs"), true).toBool());
    m_autoIndent = new QCheckBox(tr("Auto-indent new lines"), editorPage);
    m_autoIndent->setObjectName(QStringLiteral("editorAutoIndentSetting"));
    m_autoIndent->setChecked(settings.value(QStringLiteral("editor/autoIndent"), true).toBool());
    m_lineNumbers = new QCheckBox(tr("Show line numbers"), editorPage);
    m_lineNumbers->setObjectName(QStringLiteral("editorLineNumbersSetting"));
    m_lineNumbers->setChecked(settings.value(QStringLiteral("editor/lineNumbers"), true).toBool());
    m_autoSave = new QCheckBox(tr("Automatically save named files after changes"), editorPage);
    m_autoSave->setObjectName(QStringLiteral("editorAutoSaveSetting"));
    m_autoSave->setChecked(settings.value(QStringLiteral("editor/autoSave"), false).toBool());
    m_autoSaveDelay = new QSpinBox(editorPage);
    m_autoSaveDelay->setObjectName(QStringLiteral("editorAutoSaveDelaySetting"));
    m_autoSaveDelay->setRange(250, 10000);
    m_autoSaveDelay->setSingleStep(250);
    m_autoSaveDelay->setSuffix(tr(" ms"));
    m_autoSaveDelay->setValue(settings.value(QStringLiteral("editor/autoSaveDelayMs"), 1000).toInt());
    m_autoSaveDelay->setEnabled(m_autoSave->isChecked());
    connect(m_autoSave, &QCheckBox::toggled, m_autoSaveDelay, &QWidget::setEnabled);
    editorForm->addRow(tr("Font:"), m_editorFont);
    editorForm->addRow(tr("Font size:"), m_editorFontSize);
    editorForm->addRow(tr("Tab size:"), m_tabSize);
    editorForm->addRow(QString{}, m_wordWrap);
    editorForm->addRow(QString{}, m_autoClosePairs);
    editorForm->addRow(QString{}, m_autoIndent);
    editorForm->addRow(QString{}, m_lineNumbers);
    editorForm->addRow(QString{}, m_autoSave);
    editorForm->addRow(tr("Autosave delay:"), m_autoSaveDelay);
    editorLayout->addLayout(editorForm);
    editorLayout->addStretch();
    addPage(tr("Text Editor"), {tr("font"), tr("tab"), tr("wrap"), tr("indent"), tr("bracket"),
                                 tr("quote"), tr("line number"), tr("autosave"), tr("delay")}, editorPage);

    auto *terminalPage = new QWidget(m_pages);
    auto *terminalLayout = new QVBoxLayout(terminalPage);
    terminalLayout->addWidget(heading(tr("Terminal"), terminalPage));
    auto *terminalForm = new QFormLayout;
    m_terminalFont = new QFontComboBox(terminalPage);
    m_terminalFont->setObjectName(QStringLiteral("terminalFontFamily"));
    m_terminalFont->setCurrentFont(QFont(settings.value(QStringLiteral("terminal/fontFamily"),
                                                fixedFont.family()).toString()));
    m_terminalFontSize = new QSpinBox(terminalPage);
    m_terminalFontSize->setObjectName(QStringLiteral("terminalFontSize"));
    m_terminalFontSize->setRange(8, 32);
    m_terminalFontSize->setSuffix(tr(" pt"));
    m_terminalFontSize->setValue(settings.value(QStringLiteral("terminal/fontSize"),
                                                 qMax(8, fixedFont.pointSize())).toInt());
    m_terminalForeground = QColor(settings.value(QStringLiteral("terminal/foreground"),
                                                  QStringLiteral("#ffffff")).toString());
    m_terminalBackground = QColor(settings.value(QStringLiteral("terminal/background"),
                                                  QStringLiteral("#010409")).toString());
    m_terminalForegroundButton = new QPushButton(terminalPage);
    m_terminalForegroundButton->setObjectName(QStringLiteral("terminalForegroundColor"));
    m_terminalBackgroundButton = new QPushButton(terminalPage);
    m_terminalBackgroundButton->setObjectName(QStringLiteral("terminalBackgroundColor"));
    m_terminalFollowTheme = new QCheckBox(tr("Follow the application color theme"), terminalPage);
    m_terminalFollowTheme->setObjectName(QStringLiteral("terminalFollowThemeSetting"));
    m_terminalFollowTheme->setChecked(
        settings.value(QStringLiteral("terminal/followTheme"), true).toBool());
    updateColorButton(m_terminalForegroundButton, m_terminalForeground);
    updateColorButton(m_terminalBackgroundButton, m_terminalBackground);
    connect(m_terminalForegroundButton, &QPushButton::clicked, this, [this] {
        const QColor selected = QColorDialog::getColor(m_terminalForeground, this,
                                                        tr("Terminal Text Color"));
        if (selected.isValid()) {
            m_terminalForeground = selected;
            updateColorButton(m_terminalForegroundButton, selected);
        }
    });
    connect(m_terminalBackgroundButton, &QPushButton::clicked, this, [this] {
        const QColor selected = QColorDialog::getColor(m_terminalBackground, this,
                                                        tr("Terminal Background Color"));
        if (selected.isValid()) {
            m_terminalBackground = selected;
            updateColorButton(m_terminalBackgroundButton, selected);
        }
    });
    const auto updateTerminalColorControls = [this] {
        const bool custom = !m_terminalFollowTheme->isChecked();
        m_terminalForegroundButton->setEnabled(custom);
        m_terminalBackgroundButton->setEnabled(custom);
    };
    connect(m_terminalFollowTheme, &QCheckBox::toggled,
            this, updateTerminalColorControls);
    updateTerminalColorControls();
    terminalForm->addRow(tr("Font:"), m_terminalFont);
    terminalForm->addRow(tr("Font size:"), m_terminalFontSize);
    terminalForm->addRow(QString{}, m_terminalFollowTheme);
    terminalForm->addRow(tr("Text color:"), m_terminalForegroundButton);
    terminalForm->addRow(tr("Background:"), m_terminalBackgroundButton);
    terminalLayout->addLayout(terminalForm);
    terminalLayout->addStretch();
    addPage(tr("Terminal"), {tr("shell"), tr("font"), tr("text color"), tr("background")},
            terminalPage);

    auto *pluginsPage = new QWidget(m_pages);
    auto *pluginsLayout = new QVBoxLayout(pluginsPage);
    pluginsLayout->addWidget(heading(tr("Plugins and Extensions"), pluginsPage));
    auto *pluginDescription = new QLabel(
        tr("Install, update, disable, or remove language plugins and native extensions."),
        pluginsPage);
    pluginDescription->setWordWrap(true);
    pluginsLayout->addWidget(pluginDescription);
    auto *manageLanguages = new QPushButton(tr("Open Plugin Explorer…"), pluginsPage);
    manageLanguages->setObjectName(QStringLiteral("settingsManagePlugins"));
    auto *manageExtensions = new QPushButton(tr("Manage Native Extensions…"), pluginsPage);
    manageExtensions->setObjectName(QStringLiteral("settingsManageExtensions"));
    pluginsLayout->addWidget(manageLanguages);
    pluginsLayout->addWidget(manageExtensions);
    pluginsLayout->addStretch();
    connect(manageLanguages, &QPushButton::clicked, this, [this] {
        PluginManagerDialog dialog(m_languages, this);
        dialog.exec();
    });
    connect(manageExtensions, &QPushButton::clicked, this, [this] {
        ExtensionManagerDialog dialog(m_extensions, this);
        dialog.exec();
    });
    addPage(tr("Plugins"), {tr("extensions"), tr("install"), tr("update"), tr("remove")},
            pluginsPage);

    if (m_languages) {
        const auto pluginPages = m_languages->createSettingsPages(m_pages);
        for (const auto &page : pluginPages)
            addPage(page.title, page.keywords, page.widget);
    }
    if (m_extensions) {
        const auto extensionPages = m_extensions->createSettingsPages(m_pages);
        for (const auto &page : extensionPages) {
            addPage(page.title, page.keywords, page.widget);
            if (page.apply) m_extensionSettingsApply.push_back(page.apply);
            if (page.restoreDefaults) m_extensionSettingsRestore.push_back(page.restoreDefaults);
        }
    }

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            this, &SettingsDialog::restoreDefaults);
    connect(search, &QLineEdit::textChanged, this, &SettingsDialog::filterSettings);
    connect(m_categories, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);
    if (m_categories->count() > 0) m_categories->setCurrentRow(0);
}

void SettingsDialog::addPage(const QString &title, const QStringList &keywords, QWidget *page)
{
    auto *item = new QListWidgetItem(title, m_categories);
    item->setData(Qt::UserRole, (QStringList{title} + keywords).join(QLatin1Char(' ')));
    m_pages->addWidget(page);
}

void SettingsDialog::updateColorButton(QPushButton *button, const QColor &color)
{
    button->setText(color.name().toUpper());
    button->setStyleSheet(QStringLiteral("background: %1; color: %2;")
        .arg(color.name(), color.lightness() < 128 ? QStringLiteral("#ffffff")
                                                   : QStringLiteral("#000000")));
}

void SettingsDialog::filterSettings(const QString &query)
{
    const QString needle = query.trimmed();
    int firstVisible = -1;
    for (int index = 0; index < m_categories->count(); ++index) {
        auto *item = m_categories->item(index);
        const bool visible = needle.isEmpty()
            || item->data(Qt::UserRole).toString().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!visible);
        if (visible && firstVisible < 0) firstVisible = index;
    }
    const int current = m_categories->currentRow();
    if (firstVisible >= 0 && (current < 0 || m_categories->item(current)->isHidden()))
        m_categories->setCurrentRow(firstVisible);
}

void SettingsDialog::restoreDefaults()
{
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_theme->setCurrentIndex(m_theme->findData(QStringLiteral("dark")));
    m_uiFontSize->setValue(QApplication::font().pointSize());
    m_editorFont->setCurrentFont(fixedFont);
    m_editorFontSize->setValue(11);
    m_tabSize->setValue(4);
    m_wordWrap->setChecked(false);
    m_autoClosePairs->setChecked(true);
    m_autoIndent->setChecked(true);
    m_lineNumbers->setChecked(true);
    m_autoSave->setChecked(false);
    m_autoSaveDelay->setValue(1000);
    m_terminalFont->setCurrentFont(fixedFont);
    m_terminalFontSize->setValue(qMax(8, fixedFont.pointSize()));
    m_terminalFollowTheme->setChecked(true);
    m_terminalForeground = QColor(QStringLiteral("#ffffff"));
    m_terminalBackground = QColor(QStringLiteral("#010409"));
    updateColorButton(m_terminalForegroundButton, m_terminalForeground);
    updateColorButton(m_terminalBackgroundButton, m_terminalBackground);
    for (const auto &restore : m_extensionSettingsRestore) restore();
}

void SettingsDialog::accept()
{
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/theme"), m_theme->currentData());
    settings.setValue(QStringLiteral("appearance/uiFontSize"), m_uiFontSize->value());
    settings.setValue(QStringLiteral("editor/fontFamily"), m_editorFont->currentFont().family());
    settings.setValue(QStringLiteral("editor/fontSize"), m_editorFontSize->value());
    settings.setValue(QStringLiteral("editor/tabSize"), m_tabSize->value());
    settings.setValue(QStringLiteral("editor/wordWrap"), m_wordWrap->isChecked());
    settings.setValue(QStringLiteral("editor/autoClosePairs"), m_autoClosePairs->isChecked());
    settings.setValue(QStringLiteral("editor/autoIndent"), m_autoIndent->isChecked());
    settings.setValue(QStringLiteral("editor/lineNumbers"), m_lineNumbers->isChecked());
    settings.setValue(QStringLiteral("editor/autoSave"), m_autoSave->isChecked());
    settings.setValue(QStringLiteral("editor/autoSaveDelayMs"), m_autoSaveDelay->value());
    settings.setValue(QStringLiteral("terminal/fontFamily"), m_terminalFont->currentFont().family());
    settings.setValue(QStringLiteral("terminal/fontSize"), m_terminalFontSize->value());
    settings.setValue(QStringLiteral("terminal/followTheme"), m_terminalFollowTheme->isChecked());
    settings.setValue(QStringLiteral("terminal/foreground"), m_terminalForeground.name());
    settings.setValue(QStringLiteral("terminal/background"), m_terminalBackground.name());
    for (const auto &apply : m_extensionSettingsApply) apply();
    QDialog::accept();
}
