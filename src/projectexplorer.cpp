#include "projectexplorer.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {
QToolButton *makeButton(QWidget *parent, const QIcon &icon, const QString &tooltip)
{
    auto *button = new QToolButton(parent);
    button->setIcon(icon);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFixedSize(25, 25);
    return button;
}
}

ProjectExplorer::ProjectExplorer(QWidget *parent)
    : QWidget(parent)
    , m_model(new QFileSystemModel(this))
    , m_tree(new QTreeView(this))
    , m_workspaceLabel(new QLabel(tr("NO FOLDER OPENED"), this))
{
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    m_model->setReadOnly(false);
    m_model->setIconProvider(new QFileIconProvider());

    m_tree->setObjectName(QStringLiteral("projectTree"));
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    m_tree->setAnimated(true);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    for (int column = 1; column < m_model->columnCount(); ++column) m_tree->hideColumn(column);

    m_workspaceLabel->setObjectName(QStringLiteral("workspaceLabel"));
    m_workspaceLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *newFileButton = makeButton(this, style()->standardIcon(QStyle::SP_FileIcon), tr("New File"));
    auto *newFolderButton = makeButton(this, style()->standardIcon(QStyle::SP_DirIcon), tr("New Folder"));
    auto *refreshButton = makeButton(this, style()->standardIcon(QStyle::SP_BrowserReload), tr("Refresh Explorer"));
    auto *collapseButton = makeButton(this, style()->standardIcon(QStyle::SP_TitleBarShadeButton), tr("Collapse Folders"));

    auto *header = new QHBoxLayout;
    header->setContentsMargins(10, 3, 5, 3);
    header->setSpacing(1);
    header->addWidget(m_workspaceLabel);
    header->addWidget(newFileButton);
    header->addWidget(newFolderButton);
    header->addWidget(refreshButton);
    header->addWidget(collapseButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeView::doubleClicked, this, &ProjectExplorer::activate);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &ProjectExplorer::showContextMenu);
    connect(newFileButton, &QToolButton::clicked, this, &ProjectExplorer::createFile);
    connect(newFolderButton, &QToolButton::clicked, this, &ProjectExplorer::createFolder);
    connect(refreshButton, &QToolButton::clicked, this, &ProjectExplorer::refresh);
    connect(collapseButton, &QToolButton::clicked, m_tree, &QTreeView::collapseAll);
}

void ProjectExplorer::setRootPath(const QString &directory)
{
    const QFileInfo info(directory);
    if (!info.isDir()) return;
    m_rootPath = info.absoluteFilePath();
    m_workspaceLabel->setText(info.fileName().toUpper());
    m_workspaceLabel->setToolTip(QDir::toNativeSeparators(m_rootPath));
    m_tree->setRootIndex(m_model->setRootPath(m_rootPath));
}

QString ProjectExplorer::rootPath() const { return m_rootPath; }
QTreeView *ProjectExplorer::treeView() const { return m_tree; }
QFileSystemModel *ProjectExplorer::model() const { return m_model; }

void ProjectExplorer::activate(const QModelIndex &index)
{
    const QString path = m_model->filePath(index);
    if (QFileInfo(path).isFile()) emit fileActivated(path);
}

QString ProjectExplorer::selectedPath() const
{
    return m_model->filePath(m_tree->currentIndex());
}

QString ProjectExplorer::targetDirectory() const
{
    const QFileInfo selected(selectedPath());
    if (selected.isDir()) return selected.absoluteFilePath();
    if (selected.isFile()) return selected.absolutePath();
    return m_rootPath;
}

void ProjectExplorer::createEntry(bool directory)
{
    if (m_rootPath.isEmpty()) return;
    bool accepted = false;
    const QString label = directory ? tr("Folder name:") : tr("File name:");
    const QString name = QInputDialog::getText(this, directory ? tr("New Folder") : tr("New File"),
                                               label, QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        QMessageBox::warning(this, tr("Invalid Name"), tr("Enter a single file or folder name."));
        return;
    }

    const QString path = QDir(targetDirectory()).filePath(name);
    bool created = false;
    if (directory) {
        created = QDir().mkpath(path);
    } else if (!QFileInfo::exists(path)) {
        QFile file(path);
        created = file.open(QIODevice::WriteOnly);
    }
    if (!created) {
        QMessageBox::warning(this, tr("Could Not Create Item"),
                             tr("Could not create %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (!directory) emit fileActivated(path);
}

void ProjectExplorer::createFile() { createEntry(false); }
void ProjectExplorer::createFolder() { createEntry(true); }

void ProjectExplorer::renameSelected()
{
    const QModelIndex index = m_tree->currentIndex();
    if (index.isValid()) m_tree->edit(index);
}

void ProjectExplorer::removeSelected()
{
    const QString path = selectedPath();
    if (path.isEmpty() || path == m_rootPath) return;
    const auto answer = QMessageBox::warning(this, tr("Delete Item"),
        tr("Permanently delete %1?").arg(QFileInfo(path).fileName()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;
    const QFileInfo info(path);
    const bool removed = info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    if (!removed) QMessageBox::warning(this, tr("Delete Failed"), tr("The item could not be deleted."));
}

void ProjectExplorer::refresh()
{
    if (m_rootPath.isEmpty()) return;
    const QString root = m_rootPath;
    m_tree->setRootIndex(m_model->setRootPath(QString{}));
    m_tree->setRootIndex(m_model->setRootPath(root));
}

void ProjectExplorer::showContextMenu(const QPoint &position)
{
    const QModelIndex index = m_tree->indexAt(position);
    if (index.isValid()) m_tree->setCurrentIndex(index);
    QMenu menu(this);
    menu.addAction(tr("New File"), this, &ProjectExplorer::createFile);
    menu.addAction(tr("New Folder"), this, &ProjectExplorer::createFolder);
    if (index.isValid()) {
        menu.addSeparator();
        menu.addAction(tr("Rename"), this, &ProjectExplorer::renameSelected);
        menu.addAction(tr("Delete"), this, &ProjectExplorer::removeSelected);
    }
    menu.addSeparator();
    menu.addAction(tr("Refresh"), this, &ProjectExplorer::refresh);
    menu.addAction(tr("Collapse Folders"), m_tree, &QTreeView::collapseAll);
    menu.exec(m_tree->viewport()->mapToGlobal(position));
}
