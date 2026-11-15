#pragma once

#include <QWidget>
#include <QDirIterator>

#include <memory>

class QLineEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

class ProjectSearchWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectSearchWidget(QWidget *parent = nullptr);
    void setRootPath(const QString &path);
    void focusSearch();

signals:
    void resultActivated(const QString &path, int line, int column);

private slots:
    void startSearch();
    void scanBatch();
    void replaceAll();

private:
    void addResult(const QString &path, int line, int column, const QString &preview);

    QString m_rootPath;
    QString m_searchText;
    QLineEdit *m_search = nullptr;
    QLineEdit *m_replace = nullptr;
    QPushButton *m_searchButton = nullptr;
    QPushButton *m_replaceButton = nullptr;
    QTreeWidget *m_results = nullptr;
    QTimer *m_timer = nullptr;
    std::unique_ptr<QDirIterator> m_iterator;
};
