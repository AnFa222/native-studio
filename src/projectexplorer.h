#pragma once

#include <QWidget>

class QFileSystemModel;
class QLabel;
class QModelIndex;
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

private slots:
    void activate(const QModelIndex &index);
    void showContextMenu(const QPoint &position);
    void createFile();
    void createFolder();
    void renameSelected();
    void removeSelected();
    void refresh();

private:
    [[nodiscard]] QString selectedPath() const;
    [[nodiscard]] QString targetDirectory() const;
    void createEntry(bool directory);

    QFileSystemModel *m_model = nullptr;
    QTreeView *m_tree = nullptr;
    QLabel *m_workspaceLabel = nullptr;
    QString m_rootPath;
};
