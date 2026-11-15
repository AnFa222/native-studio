#pragma once

#include <QDialog>
#include <QDirIterator>
#include <QStringList>

#include <memory>

class QLineEdit;
class QListWidget;
class QTimer;

class QuickOpenDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit QuickOpenDialog(const QString &rootPath, QWidget *parent = nullptr);
    [[nodiscard]] QString selectedFile() const;

private slots:
    void scanBatch();
    void filterFiles(const QString &query);
    void acceptSelection();

private:
    QString m_rootPath;
    QString m_selectedFile;
    QStringList m_relativeFiles;
    std::unique_ptr<QDirIterator> m_iterator;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_results = nullptr;
    QTimer *m_scanTimer = nullptr;
};
