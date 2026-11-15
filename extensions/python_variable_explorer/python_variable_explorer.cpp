#include "extension_api.h"
#include "variableexplorerwidget.h"

#include <QByteArray>
#include <QFileInfo>

#include <cstddef>

namespace {
template <size_t Size>
constexpr NsStringView view(const char (&text)[Size]) { return {text, Size - 1}; }

const NsHostApi *hostApi = nullptr;
PythonDebuggerWidget *activeView = nullptr;
QString selectedFile;

QString activeDocumentPath()
{
    if (!hostApi || !hostApi->get_active_document_path) return {};
    const size_t required = hostApi->get_active_document_path(hostApi->host_context, nullptr, 0);
    if (required == 0) return {};
    QByteArray path(qsizetype(required), Qt::Uninitialized);
    hostApi->get_active_document_path(hostApi->host_context, path.data(), required);
    return QString::fromUtf8(path);
}

void revealLocation(const QString &path, int line)
{
    if (!hostApi || !hostApi->reveal_file_line || path.isEmpty() || line < 1) return;
    const QByteArray encoded = path.toUtf8();
    hostApi->reveal_file_line(hostApi->host_context,
        {encoded.constData(), size_t(encoded.size())}, uint32_t(line));
}

void *createDebuggerView(void *, void *nativeParent, NsStringView)
{
    activeView = new PythonDebuggerWidget(static_cast<QWidget *>(nativeParent));
    activeView->setPathProvider(&activeDocumentPath);
    activeView->setLocationRevealer(&revealLocation);
    if (activeDocumentPath().isEmpty() && !selectedFile.isEmpty())
        activeView->setScriptPath(selectedFile);
    return activeView;
}

void projectChanged(void *, void *, NsStringView) {}

void openDebugger(void *, const NsCommandContext *context)
{
    selectedFile.clear();
    if (context && context->active_file_path.data && context->active_file_path.size > 0)
        selectedFile = QString::fromUtf8(context->active_file_path.data,
                                         qsizetype(context->active_file_path.size));
    const QFileInfo file(selectedFile);
    if (!file.isFile() || (file.suffix().compare(QStringLiteral("py"), Qt::CaseInsensitive) != 0
        && file.suffix().compare(QStringLiteral("pyw"), Qt::CaseInsensitive) != 0)) {
        hostApi->show_message(hostApi->host_context,
            view("Open and save a Python file before debugging."), 3500);
        return;
    }
    if (!hostApi->show_view(hostApi->host_context, view("python.debugger"))) return;
    if (activeView) activeView->setScriptPath(selectedFile);
}

int32_t activate(void *, const NsHostApi *host, NsExtensionHandle extension)
{
    const size_t requiredSize = offsetof(NsHostApi, reveal_file_line)
        + sizeof(host->reveal_file_line);
    if (!host || host->abi_version != NS_EXTENSION_ABI_VERSION
        || host->struct_size < requiredSize || !host->register_command
        || !host->register_view || !host->show_view || !host->get_active_document_path
        || !host->reveal_file_line) return -1;
    hostApi = host;
    const NsSettingSpec settings[]{
        {sizeof(NsSettingSpec), view("python_path"), view("Python interpreter"),
         view("Executable name or absolute path used by the debugger process."),
         NS_SETTING_TEXT, view("python"), 0, 0, nullptr, 0}
    };
    const NsSettingsPageSpec settingsPage{
        sizeof(NsSettingsPageSpec), view("Python Debugger"),
        view("Configure the native Python debugging extension."),
        view("python interpreter debug breakpoints variables"), settings, 1
    };
    const NsCommandSpec command{
        sizeof(NsCommandSpec), view("dev.nativestudio.python_debugger.open"),
        view("Open Python Debugger"), view("Python"), view("F5"),
        &openDebugger, nullptr
    };
    const NsViewSpec debuggerView{
        sizeof(NsViewSpec), view("python.debugger"), view("PYTHON DEBUGGER"),
        view("pythonDebuggerDock"), NS_VIEW_LOCATION_RIGHT,
        &createDebuggerView, &projectChanged, nullptr
    };
    if (host->register_settings_page
        && !host->register_settings_page(host->host_context, extension, &settingsPage)) return -1;
    return host->register_view(host->host_context, extension, &debuggerView)
        && host->register_command(host->host_context, extension, &command) ? 0 : -1;
}

void deactivate(void *)
{
    activeView = nullptr;
    hostApi = nullptr;
}

const NsExtensionDescriptor descriptor{
    sizeof(NsExtensionDescriptor), NS_EXTENSION_ABI_VERSION,
    view("dev.nativestudio.python_variable_explorer"), view("Python Debugger"),
    view("2.0.0"), nullptr, &activate, &deactivate
};
}

NS_EXTENSION_EXPORT const NsExtensionDescriptor *ns_extension_entry(void)
{
    return &descriptor;
}
