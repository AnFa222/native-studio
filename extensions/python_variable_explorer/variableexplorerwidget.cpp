#include "variableexplorerwidget.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstring>

namespace {
constexpr auto VariablesMarker = "__NS_VARIABLES__";
constexpr auto ErrorMarker = "__NS_ERROR__";
constexpr auto Harness = R"PY(import json, reprlib, runpy, sys
path = sys.argv[1]
try:
    values = runpy.run_path(path, run_name="__main__")
    result = []
    short = reprlib.Repr()
    short.maxstring = 240
    short.maxother = 240
    for name, value in values.items():
        if name.startswith("__"):
            continue
        try:
            rendered = short.repr(value)
        except BaseException:
            rendered = "<unrepresentable>"
        result.append({"name": name, "type": type(value).__name__, "value": rendered})
    print("\n__NS_VARIABLES__" + json.dumps(result, ensure_ascii=False))
except BaseException as error:
    print("\n__NS_ERROR__" + repr(error))
    raise
)PY";
}

VariableExplorerWidget::VariableExplorerWidget(QWidget *parent)
    : QWidget(parent), m_fileLabel(new QLabel(tr("No Python file selected"), this))
    , m_status(new QLabel(tr("Open a Python file, then run the Variable Explorer command."), this))
    , m_variables(new QTreeWidget(this)), m_output(new QPlainTextEdit(this))
    , m_run(new QPushButton(tr("Run and Inspect"), this))
    , m_stop(new QPushButton(tr("Stop"), this)), m_process(new QProcess(this))
{
    setObjectName(QStringLiteral("pythonVariableExplorer"));
    m_fileLabel->setObjectName(QStringLiteral("variableExplorerFile"));
    m_status->setObjectName(QStringLiteral("variableExplorerStatus"));
    m_status->setWordWrap(true);
    m_variables->setObjectName(QStringLiteral("variableExplorerTree"));
    m_variables->setColumnCount(3);
    m_variables->setHeaderLabels({tr("Name"), tr("Type"), tr("Value")});
    m_variables->setRootIsDecorated(false);
    m_variables->setAlternatingRowColors(true);
    m_variables->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_variables->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_variables->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_output->setObjectName(QStringLiteral("variableExplorerOutput"));
    m_output->setReadOnly(true);
    m_output->setPlaceholderText(tr("Program output appears here."));
    m_output->setMaximumHeight(110);
    m_run->setObjectName(QStringLiteral("variableExplorerRun"));
    m_stop->setObjectName(QStringLiteral("variableExplorerStop"));
    m_stop->setEnabled(false);

    auto *actions = new QHBoxLayout;
    actions->addWidget(m_run);
    actions->addWidget(m_stop);
    actions->addStretch();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(7);
    layout->addWidget(m_fileLabel);
    layout->addLayout(actions);
    layout->addWidget(m_variables, 1);
    layout->addWidget(m_status);
    layout->addWidget(m_output);

    connect(m_run, &QPushButton::clicked, this, &VariableExplorerWidget::runScript);
    connect(m_stop, &QPushButton::clicked, this, &VariableExplorerWidget::stopScript);
    connect(m_process, &QProcess::finished, this, &VariableExplorerWidget::processFinished);
}

void VariableExplorerWidget::setScriptPath(const QString &path)
{
    m_scriptPath = QFileInfo(path).absoluteFilePath();
    m_fileLabel->setText(m_scriptPath.isEmpty()
        ? tr("No Python file selected") : QDir::toNativeSeparators(m_scriptPath));
    m_status->setText(m_scriptPath.isEmpty()
        ? tr("Open a Python file, then run the Variable Explorer command.")
        : tr("Ready to run the file in an isolated Python process."));
}

void VariableExplorerWidget::runScript()
{
    if (m_process->state() != QProcess::NotRunning || m_scriptPath.isEmpty()) return;
    const QFileInfo file(m_scriptPath);
    if (!file.isFile() || (file.suffix().compare(QStringLiteral("py"), Qt::CaseInsensitive) != 0
        && file.suffix().compare(QStringLiteral("pyw"), Qt::CaseInsensitive) != 0)) {
        m_status->setText(tr("Select a saved Python file before running the Variable Explorer."));
        return;
    }
    QSettings settings;
    const QString interpreter = settings.value(
        QStringLiteral("extensions/settings/dev.nativestudio.python_variable_explorer/python_path"),
        QStringLiteral("python")).toString().trimmed();
    m_variables->clear();
    m_output->clear();
    m_status->setText(tr("Running %1…").arg(file.fileName()));
    m_run->setEnabled(false);
    m_stop->setEnabled(true);
    m_process->setWorkingDirectory(file.absolutePath());
    m_process->start(interpreter.isEmpty() ? QStringLiteral("python") : interpreter,
                     {QStringLiteral("-u"), QStringLiteral("-c"),
                      QString::fromUtf8(Harness), file.absoluteFilePath()});
}

void VariableExplorerWidget::stopScript()
{
    if (m_process->state() != QProcess::NotRunning) m_process->kill();
}

void VariableExplorerWidget::processFinished()
{
    m_run->setEnabled(true);
    m_stop->setEnabled(false);
    const QByteArray standardOutput = m_process->readAllStandardOutput();
    const QByteArray standardError = m_process->readAllStandardError();
    const qsizetype marker = standardOutput.lastIndexOf(VariablesMarker);
    const qsizetype errorMarker = standardOutput.lastIndexOf(ErrorMarker);
    QByteArray visibleOutput = standardOutput;
    if (marker >= 0) {
        showVariables(standardOutput.mid(marker + qsizetype(strlen(VariablesMarker))).trimmed());
        visibleOutput = standardOutput.left(marker).trimmed();
        m_status->setText(tr("Captured %1 variable(s).").arg(m_variables->topLevelItemCount()));
    } else if (errorMarker >= 0) {
        visibleOutput = standardOutput.left(errorMarker).trimmed();
        m_status->setText(tr("The script stopped with an error. See output below."));
    } else {
        m_status->setText(tr("No variables were captured. Verify the configured Python interpreter."));
    }
    const QString combined = QString::fromUtf8(visibleOutput)
        + (standardError.isEmpty() ? QString{} : QStringLiteral("\n") + QString::fromUtf8(standardError));
    m_output->setPlainText(combined.trimmed());
}

void VariableExplorerWidget::showVariables(const QByteArray &json)
{
    const QJsonArray values = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &value : values) {
        const QJsonObject object = value.toObject();
        auto *item = new QTreeWidgetItem(m_variables);
        item->setText(0, object.value(QStringLiteral("name")).toString());
        item->setText(1, object.value(QStringLiteral("type")).toString());
        item->setText(2, object.value(QStringLiteral("value")).toString());
        item->setToolTip(2, item->text(2));
    }
}
