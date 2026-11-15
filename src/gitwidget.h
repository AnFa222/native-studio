#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class GitWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit GitWidget(QWidget *parent = nullptr);
    ~GitWidget() override;
    void setRepositoryPath(const QString &path);

signals:
    void fileActivated(const QString &path);

private slots:
    void refresh();
    void showDiff(QTreeWidgetItem *item);
    void stageSelected();
    void unstageSelected();
    void commit();

private:
    void runGit(const QStringList &arguments, const std::function<void(const QString &)> &finished);
    [[nodiscard]] QString selectedPath() const;

    QString m_rootPath;
    QLabel *m_branch = nullptr;
    QTreeWidget *m_files = nullptr;
    QPlainTextEdit *m_diff = nullptr;
    QLineEdit *m_message = nullptr;
    QPushButton *m_commit = nullptr;
};
