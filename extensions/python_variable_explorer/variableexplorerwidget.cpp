#include "variableexplorerwidget.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cstring>

namespace {
constexpr auto DebugMarker = "__NS_DEBUG__";
constexpr auto Harness = R"PY(import json, os, reprlib, sys, traceback
target = os.path.abspath(sys.argv[1])
breakpoints = {int(value) for value in sys.argv[2].split(",") if value.strip().isdigit()}
mode = "step"
target_depth = 0
short = reprlib.Repr()
short.maxstring = 240
short.maxother = 240

def emit(payload):
    print("__NS_DEBUG__" + json.dumps(payload, ensure_ascii=False), flush=True)

def is_target(frame):
    return os.path.abspath(frame.f_code.co_filename) == target

def depth(frame):
    result = 0
    while frame:
        if is_target(frame):
            result += 1
        frame = frame.f_back
    return result

def values(frame):
    result = []
    for name, value in sorted(frame.f_locals.items()):
        if name.startswith("__"):
            continue
        try:
            rendered = short.repr(value)
        except BaseException:
            rendered = "<unrepresentable>"
        result.append({"name": name, "type": type(value).__name__, "value": rendered})
    return result

def stack(frame):
    result = []
    while frame:
        if is_target(frame):
            result.append({"function": frame.f_code.co_name, "line": frame.f_lineno})
        frame = frame.f_back
    return result

def pause(frame, reason):
    global mode, target_depth
    emit({"event": "paused", "reason": reason, "path": target, "line": frame.f_lineno,
          "function": frame.f_code.co_name, "variables": values(frame), "stack": stack(frame)})
    while True:
        command = sys.stdin.readline()
        if not command:
            raise SystemExit(0)
        command = command.strip()
        if command == "continue":
            mode = "continue"
            return
        if command == "step":
            mode = "step"
            return
        if command == "next":
            mode = "next"
            target_depth = depth(frame)
            return
        if command == "out":
            mode = "out"
            target_depth = depth(frame)
            return
        if command == "stop":
            raise SystemExit(0)

def trace(frame, event, arg):
    if event == "line" and is_target(frame):
        current_depth = depth(frame)
        line = frame.f_lineno
        should_pause = (line in breakpoints or mode == "step"
                        or (mode == "next" and current_depth <= target_depth)
                        or (mode == "out" and current_depth < target_depth))
        if should_pause:
            reason = "breakpoint" if line in breakpoints and mode == "continue" else "step"
            pause(frame, reason)
    return trace

namespace = {"__name__": "__main__", "__file__": target, "__builtins__": __builtins__}
try:
    with open(target, "rb") as source:
        code = compile(source.read(), target, "exec")
    sys.settrace(trace)
    exec(code, namespace, namespace)
except SystemExit:
    pass
except BaseException as error:
    emit({"event": "exception", "message": repr(error), "traceback": traceback.format_exc()})
finally:
    sys.settrace(None)
    emit({"event": "terminated"})
)PY";

QToolButton *debugButton(QWidget *parent, const QString &text, const QString &tooltip,
                         const QString &name)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setFixedSize(28, 26);
    return button;
}
}

PythonDebuggerWidget::PythonDebuggerWidget(QWidget *parent)
    : QWidget(parent), m_fileLabel(new QLabel(tr("No Python file selected"), this))
    , m_status(new QLabel(tr("Open a saved Python file to start debugging."), this))
    , m_variables(new QTreeWidget(this)), m_stack(new QTreeWidget(this))
    , m_breakpoints(new QListWidget(this)), m_breakpointLine(new QSpinBox(this))
    , m_output(new QPlainTextEdit(this)), m_process(new QProcess(this))
{
    setObjectName(QStringLiteral("pythonDebugger"));
    m_fileLabel->setObjectName(QStringLiteral("pythonDebuggerFile"));
    m_status->setObjectName(QStringLiteral("pythonDebuggerStatus"));
    m_status->setWordWrap(true);
    m_start = debugButton(this, QStringLiteral("▶"), tr("Start Debugging"),
                          QStringLiteral("pythonDebuggerStart"));
    m_start->setProperty("accent", true);
    m_continue = debugButton(this, QStringLiteral("▶"), tr("Continue"),
                             QStringLiteral("pythonDebuggerContinue"));
    m_stepInto = debugButton(this, QStringLiteral("↘"), tr("Step Into"),
                             QStringLiteral("pythonDebuggerStepInto"));
    m_stepOver = debugButton(this, QStringLiteral("→"), tr("Step Over"),
                             QStringLiteral("pythonDebuggerStepOver"));
    m_stepOut = debugButton(this, QStringLiteral("↗"), tr("Step Out"),
                            QStringLiteral("pythonDebuggerStepOut"));
    m_stop = debugButton(this, QStringLiteral("■"), tr("Stop Debugging"),
                         QStringLiteral("pythonDebuggerStop"));
    m_stop->setProperty("danger", true);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(3);
    for (QToolButton *button : {m_start, m_continue, m_stepInto, m_stepOver, m_stepOut, m_stop})
        controls->addWidget(button);
    controls->addStretch();

    m_variables->setObjectName(QStringLiteral("pythonDebuggerVariables"));
    m_variables->setColumnCount(3);
    m_variables->setHeaderLabels({tr("Name"), tr("Type"), tr("Value")});
    m_variables->setRootIsDecorated(false);
    m_variables->setAlternatingRowColors(true);
    m_variables->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_variables->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_variables->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_stack->setObjectName(QStringLiteral("pythonDebuggerStack"));
    m_stack->setHeaderLabels({tr("Call Stack")});
    m_stack->setRootIsDecorated(false);
    m_stack->setMaximumHeight(130);

    m_breakpointLine->setObjectName(QStringLiteral("pythonDebuggerBreakpointLine"));
    m_breakpointLine->setRange(1, 999999);
    auto *addBreakpointButton = debugButton(this, QStringLiteral("+"), tr("Add Breakpoint"),
        QStringLiteral("pythonDebuggerAddBreakpoint"));
    auto *removeBreakpointButton = debugButton(this, QStringLiteral("−"), tr("Remove Breakpoint"),
        QStringLiteral("pythonDebuggerRemoveBreakpoint"));
    auto *breakpointControls = new QHBoxLayout;
    breakpointControls->addWidget(new QLabel(tr("Line"), this));
    breakpointControls->addWidget(m_breakpointLine, 1);
    breakpointControls->addWidget(addBreakpointButton);
    breakpointControls->addWidget(removeBreakpointButton);
    m_breakpoints->setObjectName(QStringLiteral("pythonDebuggerBreakpoints"));
    m_breakpoints->setMaximumHeight(90);

    m_output->setObjectName(QStringLiteral("pythonDebuggerOutput"));
    m_output->setReadOnly(true);
    m_output->setPlaceholderText(tr("Program output and exceptions appear here."));
    m_output->setMaximumHeight(130);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(m_fileLabel);
    layout->addLayout(controls);
    layout->addWidget(new QLabel(tr("VARIABLES"), this));
    layout->addWidget(m_variables, 1);
    layout->addWidget(m_stack);
    layout->addWidget(new QLabel(tr("BREAKPOINTS"), this));
    layout->addLayout(breakpointControls);
    layout->addWidget(m_breakpoints);
    layout->addWidget(m_status);
    layout->addWidget(m_output);

    connect(m_start, &QToolButton::clicked, this, &PythonDebuggerWidget::startDebugging);
    connect(m_stop, &QToolButton::clicked, this, &PythonDebuggerWidget::stopDebugging);
    connect(m_continue, &QToolButton::clicked, this, &PythonDebuggerWidget::continueDebugging);
    connect(m_stepInto, &QToolButton::clicked, this, &PythonDebuggerWidget::stepInto);
    connect(m_stepOver, &QToolButton::clicked, this, &PythonDebuggerWidget::stepOver);
    connect(m_stepOut, &QToolButton::clicked, this, &PythonDebuggerWidget::stepOut);
    connect(addBreakpointButton, &QToolButton::clicked, this, &PythonDebuggerWidget::addBreakpoint);
    connect(removeBreakpointButton, &QToolButton::clicked, this, &PythonDebuggerWidget::removeBreakpoint);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PythonDebuggerWidget::readStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &PythonDebuggerWidget::readStandardError);
    connect(m_process, &QProcess::finished, this, &PythonDebuggerWidget::processFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        m_status->setText(tr("Debugger process failed to start. Check the Python interpreter setting."));
        m_start->setEnabled(true);
        m_stop->setEnabled(false);
    });
    setPaused(false);
}

PythonDebuggerWidget::~PythonDebuggerWidget()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void PythonDebuggerWidget::setScriptPath(const QString &path)
{
    m_scriptPath = path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
    m_fileLabel->setText(m_scriptPath.isEmpty()
        ? tr("No Python file selected") : QDir::toNativeSeparators(m_scriptPath));
    m_status->setText(m_scriptPath.isEmpty()
        ? tr("Open a saved Python file to start debugging.") : tr("Ready to debug."));
}

void PythonDebuggerWidget::setPathProvider(std::function<QString()> provider)
{
    m_pathProvider = std::move(provider);
    if (m_pathProvider) {
        const QString path = m_pathProvider();
        if (!path.isEmpty()) setScriptPath(path);
    }
}

void PythonDebuggerWidget::setLocationRevealer(
    std::function<void(const QString &, int)> revealer)
{
    m_locationRevealer = std::move(revealer);
}

QStringList PythonDebuggerWidget::breakpointArguments() const
{
    QStringList lines;
    for (int row = 0; row < m_breakpoints->count(); ++row)
        lines.push_back(m_breakpoints->item(row)->text());
    return lines;
}

void PythonDebuggerWidget::startDebugging()
{
    if (m_process->state() != QProcess::NotRunning) return;
    if (m_pathProvider) {
        const QString path = m_pathProvider();
        if (!path.isEmpty()) setScriptPath(path);
    }
    const QFileInfo file(m_scriptPath);
    if (!file.isFile() || (file.suffix().compare(QStringLiteral("py"), Qt::CaseInsensitive) != 0
        && file.suffix().compare(QStringLiteral("pyw"), Qt::CaseInsensitive) != 0)) {
        m_status->setText(tr("Select a saved Python file before debugging."));
        return;
    }
    const QString interpreter = QSettings().value(
        QStringLiteral("extensions/settings/dev.nativestudio.python_variable_explorer/python_path"),
        QStringLiteral("python")).toString().trimmed();
    m_variables->clear();
    m_stack->clear();
    m_output->clear();
    m_stdoutBuffer.clear();
    m_status->setText(tr("Starting debugger…"));
    m_start->setEnabled(false);
    m_stop->setEnabled(true);
    m_process->setWorkingDirectory(file.absolutePath());
    m_process->start(interpreter.isEmpty() ? QStringLiteral("python") : interpreter,
        {QStringLiteral("-u"), QStringLiteral("-c"), QString::fromUtf8(Harness),
         file.absoluteFilePath(), breakpointArguments().join(QLatin1Char(','))});
}

void PythonDebuggerWidget::stopDebugging()
{
    if (m_process->state() == QProcess::NotRunning) return;
    sendCommand("stop");
    if (!m_process->waitForFinished(250)) m_process->kill();
}

void PythonDebuggerWidget::continueDebugging() { sendCommand("continue"); }
void PythonDebuggerWidget::stepInto() { sendCommand("step"); }
void PythonDebuggerWidget::stepOver() { sendCommand("next"); }
void PythonDebuggerWidget::stepOut() { sendCommand("out"); }

void PythonDebuggerWidget::sendCommand(const QByteArray &command)
{
    if (m_process->state() == QProcess::NotRunning) return;
    m_process->write(command + '\n');
    setPaused(false);
    m_status->setText(tr("Running…"));
}

void PythonDebuggerWidget::addBreakpoint()
{
    const QString line = QString::number(m_breakpointLine->value());
    if (m_breakpoints->findItems(line, Qt::MatchExactly).isEmpty()) {
        m_breakpoints->addItem(line);
        m_breakpoints->sortItems();
    }
}

void PythonDebuggerWidget::removeBreakpoint()
{
    qDeleteAll(m_breakpoints->selectedItems());
}

void PythonDebuggerWidget::readStandardOutput()
{
    m_stdoutBuffer += m_process->readAllStandardOutput();
    qsizetype newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_stdoutBuffer.left(newline);
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.endsWith('\r')) line.chop(1);
        const qsizetype marker = line.indexOf(DebugMarker);
        if (marker >= 0) {
            if (marker > 0) m_output->appendPlainText(QString::fromUtf8(line.left(marker)));
            handleDebugMessage(line.mid(marker + qsizetype(strlen(DebugMarker))));
        } else if (!line.isEmpty()) {
            m_output->appendPlainText(QString::fromUtf8(line));
        }
    }
}

void PythonDebuggerWidget::readStandardError()
{
    const QString error = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
    if (!error.isEmpty()) m_output->appendPlainText(error);
}

void PythonDebuggerWidget::handleDebugMessage(const QByteArray &json)
{
    const QJsonObject message = QJsonDocument::fromJson(json).object();
    const QString event = message.value(QStringLiteral("event")).toString();
    if (event == QStringLiteral("paused")) {
        m_variables->clear();
        for (const QJsonValue &value : message.value(QStringLiteral("variables")).toArray()) {
            const QJsonObject variable = value.toObject();
            auto *item = new QTreeWidgetItem(m_variables);
            item->setText(0, variable.value(QStringLiteral("name")).toString());
            item->setText(1, variable.value(QStringLiteral("type")).toString());
            item->setText(2, variable.value(QStringLiteral("value")).toString());
        }
        m_stack->clear();
        for (const QJsonValue &value : message.value(QStringLiteral("stack")).toArray()) {
            const QJsonObject frame = value.toObject();
            new QTreeWidgetItem(m_stack, {tr("%1 — line %2")
                .arg(frame.value(QStringLiteral("function")).toString())
                .arg(frame.value(QStringLiteral("line")).toInt())});
        }
        const int line = message.value(QStringLiteral("line")).toInt();
        const QString path = message.value(QStringLiteral("path")).toString();
        m_status->setText(tr("Paused at line %1 in %2").arg(line)
            .arg(message.value(QStringLiteral("function")).toString()));
        setPaused(true);
        if (m_locationRevealer) m_locationRevealer(path, line);
    } else if (event == QStringLiteral("exception")) {
        m_status->setText(tr("Stopped on an unhandled exception."));
        m_output->appendPlainText(message.value(QStringLiteral("traceback")).toString());
    }
}

void PythonDebuggerWidget::setPaused(bool paused)
{
    const bool running = m_process->state() != QProcess::NotRunning;
    for (QToolButton *button : {m_continue, m_stepInto, m_stepOver, m_stepOut})
        button->setEnabled(running && paused);
    m_stop->setEnabled(running);
    m_start->setEnabled(!running);
}

void PythonDebuggerWidget::processFinished()
{
    readStandardOutput();
    readStandardError();
    setPaused(false);
    m_start->setEnabled(true);
    m_stop->setEnabled(false);
    if (m_status->text() == tr("Running…") || m_status->text() == tr("Starting debugger…"))
        m_status->setText(tr("Debug session finished."));
}
