#pragma once

#include <QDialog>
#include <QList>

class QAction;
class QEvent;
class QTableWidget;

class KeybindingsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit KeybindingsDialog(const QList<QAction *> &actions, QWidget *parent = nullptr);

protected:
    void accept() override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QList<QAction *> m_actions;
    QTableWidget *m_table = nullptr;
};
