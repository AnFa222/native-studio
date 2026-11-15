#include "projectsearchwidget.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

ProjectSearchWidget::ProjectSearchWidget(QWidget *parent)
    : QWidget(parent), m_search(new QLineEdit(this)), m_replace(new QLineEdit(this))
    , m_searchButton(new QPushButton(tr("Search"), this))
    , m_replaceButton(new QPushButton(tr("Replace All"), this))
    , m_results(new QTreeWidget(this)), m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("projectSearchWidget"));
    m_search->setObjectName(QStringLiteral("projectSearchText"));
    m_replace->setObjectName(QStringLiteral("projectReplaceText"));
    m_results->setObjectName(QStringLiteral("projectSearchResults"));
    m_search->setPlaceholderText(tr("Search in files"));
    m_replace->setPlaceholderText(tr("Replace with"));
    m_results->setHeaderLabels({tr("File"), tr("Line"), tr("Preview")});
    m_results->setRootIsDecorated(false);
    auto *controls = new QHBoxLayout;
    controls->addWidget(m_search, 2);
    controls->addWidget(m_replace, 2);
    controls->addWidget(m_searchButton);
    controls->addWidget(m_replaceButton);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(controls);
    layout->addWidget(m_results, 1);
    m_timer->setInterval(0);
    connect(m_searchButton, &QPushButton::clicked, this, &ProjectSearchWidget::startSearch);
    connect(m_search, &QLineEdit::returnPressed, this, &ProjectSearchWidget::startSearch);
    connect(m_replaceButton, &QPushButton::clicked, this, &ProjectSearchWidget::replaceAll);
    connect(m_timer, &QTimer::timeout, this, &ProjectSearchWidget::scanBatch);
    connect(m_results, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (!item) return;
        emit resultActivated(item->data(0, Qt::UserRole).toString(),
                             item->data(1, Qt::UserRole).toInt(),
                             item->data(2, Qt::UserRole).toInt());
    });
}

void ProjectSearchWidget::setRootPath(const QString &path) { m_rootPath = path; }
void ProjectSearchWidget::focusSearch() { m_search->setFocus(); m_search->selectAll(); }

void ProjectSearchWidget::startSearch()
{
    m_searchText = m_search->text();
    m_results->clear();
    if (m_rootPath.isEmpty() || m_searchText.isEmpty()) return;
    m_iterator = std::make_unique<QDirIterator>(m_rootPath, QDir::Files | QDir::NoDotAndDotDot,
                                                QDirIterator::Subdirectories);
    m_timer->start();
}

void ProjectSearchWidget::scanBatch()
{
    constexpr int batchSize = 35;
    int count = 0;
    while (count++ < batchSize && m_iterator && m_iterator->hasNext()) {
        const QString path = m_iterator->next();
        const QString relative = QDir(m_rootPath).relativeFilePath(path);
        if (relative.startsWith(QStringLiteral(".git/"), Qt::CaseInsensitive)
            || QFileInfo(path).size() > 5 * 1024 * 1024) continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = file.readAll();
        if (bytes.contains('\0')) continue;
        const QStringList lines = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
        for (int line = 0; line < lines.size(); ++line) {
            int from = 0;
            while ((from = lines.at(line).indexOf(m_searchText, from, Qt::CaseInsensitive)) >= 0) {
                addResult(path, line, from, lines.at(line).trimmed());
                from += qMax(1, m_searchText.size());
            }
        }
    }
    if (!m_iterator || !m_iterator->hasNext()) m_timer->stop();
}

void ProjectSearchWidget::addResult(const QString &path, int line, int column,
                                    const QString &preview)
{
    if (m_results->topLevelItemCount() >= 10000) {
        m_timer->stop();
        m_iterator.reset();
        return;
    }
    auto *item = new QTreeWidgetItem(m_results);
    item->setText(0, QDir(m_rootPath).relativeFilePath(path));
    item->setText(1, QString::number(line + 1));
    item->setText(2, preview.left(240));
    item->setData(0, Qt::UserRole, path);
    item->setData(1, Qt::UserRole, line);
    item->setData(2, Qt::UserRole, column);
}

void ProjectSearchWidget::replaceAll()
{
    if (m_searchText.isEmpty() || m_results->topLevelItemCount() == 0) return;
    QSet<QString> paths;
    for (int index = 0; index < m_results->topLevelItemCount(); ++index)
        paths.insert(m_results->topLevelItem(index)->data(0, Qt::UserRole).toString());
    for (const QString &path : paths) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) continue;
        QString text = QString::fromUtf8(input.readAll());
        input.close();
        text.replace(m_searchText, m_replace->text(), Qt::CaseInsensitive);
        QSaveFile output(path);
        if (output.open(QIODevice::WriteOnly)) {
            output.write(text.toUtf8());
            output.commit();
        }
    }
    startSearch();
}
