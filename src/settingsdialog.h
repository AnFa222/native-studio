#pragma once

#include <QColor>
#include <QDialog>

class ExtensionManager;
class LanguageManager;
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;

class SettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(LanguageManager *languages, ExtensionManager *extensions,
                   QWidget *parent = nullptr);

protected:
    void accept() override;

private slots:
    void filterSettings(const QString &query);
    void restoreDefaults();

private:
    void addPage(const QString &title, const QStringList &keywords, QWidget *page);
    static void updateColorButton(QPushButton *button, const QColor &color);

    LanguageManager *m_languages = nullptr;
    ExtensionManager *m_extensions = nullptr;
    QListWidget *m_categories = nullptr;
    QStackedWidget *m_pages = nullptr;
    QComboBox *m_theme = nullptr;
    QSpinBox *m_uiFontSize = nullptr;
    QFontComboBox *m_editorFont = nullptr;
    QSpinBox *m_editorFontSize = nullptr;
    QSpinBox *m_tabSize = nullptr;
    QCheckBox *m_wordWrap = nullptr;
    QCheckBox *m_autoClosePairs = nullptr;
    QCheckBox *m_lineNumbers = nullptr;
    QFontComboBox *m_terminalFont = nullptr;
    QSpinBox *m_terminalFontSize = nullptr;
    QPushButton *m_terminalForegroundButton = nullptr;
    QPushButton *m_terminalBackgroundButton = nullptr;
    QColor m_terminalForeground;
    QColor m_terminalBackground;
};
