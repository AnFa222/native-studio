#pragma once

#include <QWidget>

class QFileSystemModel;
class QEvent;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPushButton;
class QTreeView;

class ProjectExplorer final : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectExplorer(QWidget *parent = nullptr);

    void setRootPath(const QString &directory);
    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] QTreeView *treeView() const;
    [[nodiscard]] QFileSystemModel *model() const;

signals:
    void fileActivated(const QString &path);
    void openFolderRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void activate(const QModelIndex &index);
    void showContextMenu(const QPoint &position);
    void createFile();
    void createFolder();
    void renameSelected();
    void removeSelected();
    void refresh();
    void filterChanged(const QString &text);
    void updateBreadcrumb(const QModelIndex &current);

private:
    [[nodiscard]] QString selectedPath() const;
    [[nodiscard]] QString targetDirectory() const;
    void createEntry(bool directory);
    void ensureModel();

    QFileSystemModel *m_model = nullptr;
    QTreeView *m_tree = nullptr;
    QLabel *m_workspaceLabel = nullptr;
    QLabel *m_breadcrumb = nullptr;
    QLineEdit *m_filter = nullptr;
    QWidget *m_emptyState = nullptr;
    QPushButton *m_openFolder = nullptr;
    QString m_rootPath;
};
