#include "projectexplorer.h"
#include "extension_api.h"

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QPainter>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

class DecoratedFileSystemModel final : public QFileSystemModel
{
public:
    void setDecorations(const QString &sourceId,
                        const QList<ExplorerFileDecoration> &decorations)
    {
        QHash<QString, ExplorerFileDecoration> source;
        for (const auto &decoration : decorations)
            source.insert(key(decoration.path), decoration);
        if (source.isEmpty()) m_sources.remove(sourceId);
        else m_sources.insert(sourceId, std::move(source));
        emit layoutChanged();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        const QVariant base = QFileSystemModel::data(index, role);
        if (!index.isValid() || index.column() != 0) return base;
        const ExplorerFileDecoration *decoration = decorationFor(filePath(index));
        if (!decoration) return base;
        if (role == Qt::ForegroundRole) {
            const bool dark = QApplication::palette().color(QPalette::Base).lightness() < 128;
            QColor color;
            if (decoration->kind == NS_FILE_DECORATION_MODIFIED)
                color = QColor(dark ? QStringLiteral("#e2c08d") : QStringLiteral("#9a6700"));
            else if (decoration->kind == NS_FILE_DECORATION_DELETED
                     || decoration->kind == NS_FILE_DECORATION_CONFLICT)
                color = QColor(dark ? QStringLiteral("#f85149") : QStringLiteral("#cf222e"));
            else
                color = QColor(dark ? QStringLiteral("#7ee787") : QStringLiteral("#1a7f37"));
            return QBrush(color);
        }
        if (role == Qt::FontRole) {
            QFont font = QApplication::font();
            font.setWeight(QFont::DemiBold);
            return font;
        }
        if (role == Qt::ToolTipRole)
            return decoration->label.isEmpty() ? base
                : QStringLiteral("%1 — %2").arg(filePath(index), decoration->label);
        return base;
    }

private:
    static QString key(const QString &path)
    {
        QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
        normalized = normalized.toLower();
#endif
        return normalized;
    }

    const ExplorerFileDecoration *decorationFor(const QString &path) const
    {
        const QString normalized = key(path);
        for (auto iterator = m_sources.cbegin(); iterator != m_sources.cend(); ++iterator) {
            const auto found = iterator.value().constFind(normalized);
            if (found != iterator.value().cend()) return &found.value();
        }
        return nullptr;
    }

    QHash<QString, QHash<QString, ExplorerFileDecoration>> m_sources;
};

namespace {
class ExplorerTreeStyle final : public QProxyStyle
{
public:
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override
    {
        if (element != PE_IndicatorBranch || !option->state.testFlag(State_Children)) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        const QPointF center = option->rect.center();
        QPolygonF chevron;
        if (option->state.testFlag(State_Open)) {
            chevron << QPointF(center.x() - 4, center.y() - 2)
                    << QPointF(center.x(), center.y() + 2)
                    << QPointF(center.x() + 4, center.y() - 2);
        } else {
            chevron << QPointF(center.x() - 2, center.y() - 4)
                    << QPointF(center.x() + 2, center.y())
                    << QPointF(center.x() - 2, center.y() + 4);
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(option->palette.color(QPalette::Text), 1.5,
                             Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPolyline(chevron);
        painter->restore();
    }
};

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
    , m_tree(new QTreeView(this))
    , m_workspaceLabel(new QLabel(tr("NO FOLDER OPENED"), this))
    , m_breadcrumb(new QLabel(this))
    , m_filter(new QLineEdit(this))
    , m_emptyState(new QWidget(this))
    , m_openFolder(new QPushButton(tr("Open Folder"), m_emptyState))
{
    m_tree->setObjectName(QStringLiteral("projectTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setAnimated(true);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->viewport()->setAcceptDrops(true);
    m_tree->viewport()->installEventFilter(this);
    m_tree->setVisible(false);
    auto *treeStyle = new ExplorerTreeStyle;
    treeStyle->setParent(m_tree);
    m_tree->setStyle(treeStyle);

    m_workspaceLabel->setObjectName(QStringLiteral("workspaceLabel"));
    m_workspaceLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_breadcrumb->setObjectName(QStringLiteral("explorerBreadcrumb"));
    m_breadcrumb->setContentsMargins(10, 3, 6, 3);
    m_breadcrumb->setVisible(false);
    m_filter->setObjectName(QStringLiteral("explorerFilter"));
    m_filter->setPlaceholderText(tr("Filter files…"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setVisible(false);
    m_openFolder->setObjectName(QStringLiteral("explorerOpenFolder"));
    m_openFolder->setVisible(false);

    auto *newFileButton = makeButton(this, style()->standardIcon(QStyle::SP_FileIcon), tr("New File"));
    auto *newFolderButton = makeButton(this, style()->standardIcon(QStyle::SP_DirIcon), tr("New Folder"));
    auto *refreshButton = makeButton(this, style()->standardIcon(QStyle::SP_BrowserReload), tr("Refresh Explorer"));
    auto *collapseButton = makeButton(this, style()->standardIcon(QStyle::SP_TitleBarShadeButton), tr("Collapse Folders"));
    for (QToolButton *button : {newFileButton, newFolderButton, refreshButton, collapseButton}) {
        button->setProperty("workspaceAction", true);
        button->setVisible(false);
    }

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
    layout->addWidget(m_breadcrumb);
    layout->addWidget(m_filter);
    auto *emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->addStretch();
    emptyLayout->addWidget(m_openFolder, 0, Qt::AlignHCenter);
    emptyLayout->addStretch();
    layout->addWidget(m_emptyState, 1);
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeView::doubleClicked, this, &ProjectExplorer::activate);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &ProjectExplorer::showContextMenu);
    connect(newFileButton, &QToolButton::clicked, this, &ProjectExplorer::createFile);
    connect(newFolderButton, &QToolButton::clicked, this, &ProjectExplorer::createFolder);
    connect(refreshButton, &QToolButton::clicked, this, &ProjectExplorer::refresh);
    connect(collapseButton, &QToolButton::clicked, m_tree, &QTreeView::collapseAll);
    connect(m_openFolder, &QPushButton::clicked, this, &ProjectExplorer::openFolderRequested);
    connect(m_filter, &QLineEdit::textChanged, this, &ProjectExplorer::filterChanged);
}

void ProjectExplorer::ensureModel()
{
    if (m_model) return;
    m_model = new DecoratedFileSystemModel;
    m_model->setParent(this);
    m_model->setFilter(QDir::AllEntries | QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden);
    m_model->setReadOnly(false);
    m_tree->setModel(m_model);
    for (int column = 1; column < m_model->columnCount(); ++column) m_tree->hideColumn(column);
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &ProjectExplorer::updateBreadcrumb);
}

void ProjectExplorer::setRootPath(const QString &directory)
{
    const QFileInfo info(directory);
    if (!info.isDir()) return;
    ensureModel();
    m_rootPath = info.absoluteFilePath();
    m_workspaceLabel->setText(info.fileName().toUpper());
    m_workspaceLabel->setToolTip(QDir::toNativeSeparators(m_rootPath));
    m_tree->setRootIndex(m_model->setRootPath(m_rootPath));
    m_tree->setVisible(true);
    m_filter->setVisible(true);
    m_breadcrumb->setVisible(true);
    m_openFolder->setVisible(false);
    m_emptyState->setVisible(false);
    for (QToolButton *button : findChildren<QToolButton *>())
        if (button->property("workspaceAction").toBool()) button->setVisible(true);
    m_breadcrumb->setText(info.fileName());
}

void ProjectExplorer::clearRootPath()
{
    m_rootPath.clear();
    m_workspaceLabel->setText(tr("NO FOLDER OPENED"));
    m_workspaceLabel->setToolTip({});
    m_tree->setVisible(false);
    m_filter->clear();
    m_filter->setVisible(false);
    m_breadcrumb->clear();
    m_breadcrumb->setVisible(false);
    m_emptyState->setVisible(true);
    m_openFolder->setVisible(false);
    for (QToolButton *button : findChildren<QToolButton *>())
        if (button->property("workspaceAction").toBool()) button->setVisible(false);
}

QString ProjectExplorer::rootPath() const { return m_rootPath; }
QTreeView *ProjectExplorer::treeView() const { return m_tree; }
QFileSystemModel *ProjectExplorer::model() const { return m_model; }

void ProjectExplorer::setFileDecorations(
    const QString &sourceId, const QList<ExplorerFileDecoration> &decorations)
{
    ensureModel();
    QList<ExplorerFileDecoration> absolute;
    absolute.reserve(decorations.size());
    for (ExplorerFileDecoration decoration : decorations) {
        const QString candidate = QFileInfo(decoration.path).isAbsolute()
            ? decoration.path : QDir(m_rootPath).filePath(decoration.path);
        const QString relative = QDir(m_rootPath).relativeFilePath(candidate);
        if (m_rootPath.isEmpty() || relative == QStringLiteral("..")
            || relative.startsWith(QStringLiteral("../"))) continue;
        decoration.path = QFileInfo(candidate).absoluteFilePath();
        absolute.push_back(std::move(decoration));
    }
    m_model->setDecorations(sourceId, absolute);
}

bool ProjectExplorer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tree->viewport()
        && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto *drag = static_cast<QDragMoveEvent *>(event);
        if (drag->mimeData()->hasUrls()) {
            drag->acceptProposedAction();
            return true;
        }
    }
    if (watched == m_tree->viewport() && event->type() == QEvent::Drop) {
        auto *drop = static_cast<QDropEvent *>(event);
        if (m_model && !drop->mimeData()->urls().isEmpty()) {
            const QString path = drop->mimeData()->urls().constFirst().toLocalFile();
            const QString relative = QDir(m_rootPath).relativeFilePath(path);
            if (QFileInfo(path).isFile() && relative != QStringLiteral("..")
                && !relative.startsWith(QStringLiteral("../"))) {
                const QModelIndex index = m_model->index(path);
                if (index.isValid()) {
                    m_tree->setCurrentIndex(index);
                    m_tree->scrollTo(index);
                }
                emit fileActivated(path);
                drop->acceptProposedAction();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ProjectExplorer::activate(const QModelIndex &index)
{
    const QString path = m_model->filePath(index);
    if (QFileInfo(path).isFile()) emit fileActivated(path);
}

QString ProjectExplorer::selectedPath() const
{
    if (!m_model) return {};
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
    if (m_rootPath.isEmpty() || !m_model) return;
    const QString root = m_rootPath;
    m_tree->setRootIndex(m_model->setRootPath(QString{}));
    m_tree->setRootIndex(m_model->setRootPath(root));
}

void ProjectExplorer::filterChanged(const QString &text)
{
    if (!m_model) return;
    const QString filter = text.trimmed();
    m_model->setNameFilters(filter.isEmpty()
        ? QStringList{} : QStringList{QStringLiteral("*%1*").arg(filter)});
    m_model->setNameFilterDisables(false);
}

void ProjectExplorer::updateBreadcrumb(const QModelIndex &current)
{
    if (!m_model) return;
    if (!current.isValid() || m_rootPath.isEmpty()) {
        m_breadcrumb->setText(QFileInfo(m_rootPath).fileName());
        return;
    }
    const QString relative = QDir(m_rootPath).relativeFilePath(m_model->filePath(current));
    m_breadcrumb->setText(relative == QStringLiteral(".")
        ? QFileInfo(m_rootPath).fileName()
        : QFileInfo(m_rootPath).fileName() + QStringLiteral("  ›  ")
            + QDir::fromNativeSeparators(relative).replace(QLatin1Char('/'), QStringLiteral("  ›  ")));
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
