#include "keybindingsdialog.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

KeybindingsDialog::KeybindingsDialog(const QList<QAction *> &actions, QWidget *parent)
    : QDialog(parent), m_table(new QTableWidget(this))
{
    setObjectName(QStringLiteral("keybindingsDialog"));
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(760, 560);
    for (QAction *action : actions) {
        if (action && !action->objectName().isEmpty() && !action->text().isEmpty())
            m_actions.push_back(action);
    }
    std::sort(m_actions.begin(), m_actions.end(), [](const QAction *left, const QAction *right) {
        return left->text().localeAwareCompare(right->text()) < 0;
    });
    m_table->setObjectName(QStringLiteral("keybindingsTable"));
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut")});
    m_table->setRowCount(m_actions.size());
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int row = 0; row < m_actions.size(); ++row) {
        auto *name = new QTableWidgetItem(m_actions.at(row)->text().remove(QLatin1Char('&')));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, name);
        auto *editor = new QKeySequenceEdit(m_actions.at(row)->shortcut(), m_table);
        editor->setObjectName(QStringLiteral("keybinding.%1").arg(m_actions.at(row)->objectName()));
        m_table->setCellWidget(row, 1, editor);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addWidget(buttons);
}

void KeybindingsDialog::accept()
{
    QSettings settings;
    for (int row = 0; row < m_actions.size(); ++row) {
        auto *editor = qobject_cast<QKeySequenceEdit *>(m_table->cellWidget(row, 1));
        if (!editor) continue;
        QAction *action = m_actions.at(row);
        action->setShortcut(editor->keySequence());
        settings.setValue(QStringLiteral("keybindings/") + action->objectName(),
                          editor->keySequence().toString(QKeySequence::PortableText));
    }
    QDialog::accept();
}
