#pragma once

#include <QDialog>

class ExtensionManager;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class ExtensionManagerDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ExtensionManagerDialog(ExtensionManager *manager, QWidget *parent = nullptr);

private slots:
    void refresh();
    void filterRows(const QString &text);
    void updateSelection();
    void importManifest();
    void toggleSelected();
    void removeSelected();

private:
    [[nodiscard]] QString selectedId() const;

    ExtensionManager *m_manager = nullptr;
    QLineEdit *m_search = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_details = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_toggle = nullptr;
    QPushButton *m_remove = nullptr;
};
