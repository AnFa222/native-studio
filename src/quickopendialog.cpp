#include "quickopendialog.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTimer>
#include <QVBoxLayout>

QuickOpenDialog::QuickOpenDialog(const QString &rootPath, QWidget *parent)
    : QDialog(parent), m_rootPath(QFileInfo(rootPath).absoluteFilePath())
    , m_filter(new QLineEdit(this)), m_results(new QListWidget(this)), m_scanTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("quickOpenDialog"));
    setWindowTitle(tr("Quick Open"));
    resize(680, 460);
    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel(tr("Type a file name to open"), this);
    m_filter->setObjectName(QStringLiteral("quickOpenFilter"));
    m_filter->setPlaceholderText(tr("Search files by name or path…"));
    m_results->setObjectName(QStringLiteral("quickOpenResults"));
    layout->addWidget(label);
    layout->addWidget(m_filter);
    layout->addWidget(m_results, 1);

    m_iterator = std::make_unique<QDirIterator>(m_rootPath, QDir::Files | QDir::NoDotAndDotDot,
                                                QDirIterator::Subdirectories);
    m_scanTimer->setInterval(0);
    connect(m_scanTimer, &QTimer::timeout, this, &QuickOpenDialog::scanBatch);
    connect(m_filter, &QLineEdit::textChanged, this, &QuickOpenDialog::filterFiles);
    connect(m_filter, &QLineEdit::returnPressed, this, &QuickOpenDialog::acceptSelection);
    connect(m_results, &QListWidget::itemActivated, this, &QuickOpenDialog::acceptSelection);
    m_scanTimer->start();
    m_filter->setFocus();
}

QString QuickOpenDialog::selectedFile() const { return m_selectedFile; }

void QuickOpenDialog::scanBatch()
{
    constexpr int batchSize = 250;
    int scanned = 0;
    bool changed = false;
    while (scanned++ < batchSize && m_iterator && m_iterator->hasNext()) {
        const QString absolute = m_iterator->next();
        const QString relative = QDir(m_rootPath).relativeFilePath(absolute);
        if (relative.startsWith(QStringLiteral(".git/"), Qt::CaseInsensitive)
            || relative.contains(QStringLiteral("/.git/"), Qt::CaseInsensitive)) continue;
        m_relativeFiles.push_back(QDir::fromNativeSeparators(relative));
        changed = true;
        if (m_relativeFiles.size() >= 100000) {
            m_iterator.reset();
            break;
        }
    }
    if (changed) filterFiles(m_filter->text());
    if (!m_iterator || !m_iterator->hasNext()) {
        m_scanTimer->stop();
        m_relativeFiles.sort(Qt::CaseInsensitive);
        filterFiles(m_filter->text());
    }
}

void QuickOpenDialog::filterFiles(const QString &query)
{
    const QString needle = query.trimmed();
    const QString current = m_results->currentItem() ? m_results->currentItem()->text() : QString{};
    m_results->clear();
    int added = 0;
    for (const QString &path : m_relativeFiles) {
        if (!needle.isEmpty() && !path.contains(needle, Qt::CaseInsensitive)) continue;
        m_results->addItem(path);
        if (++added >= 500) break;
    }
    const auto matches = m_results->findItems(current, Qt::MatchExactly);
    if (!matches.isEmpty()) m_results->setCurrentItem(matches.first());
    else if (m_results->count() > 0) m_results->setCurrentRow(0);
}

void QuickOpenDialog::acceptSelection()
{
    auto *item = m_results->currentItem();
    if (!item) return;
    m_selectedFile = QDir(m_rootPath).filePath(item->text());
    accept();
}
