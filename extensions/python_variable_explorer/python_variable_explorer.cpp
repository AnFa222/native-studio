#include "extension_api.h"
#include "variableexplorerwidget.h"

#include <QFileInfo>

#include <cstddef>
#include <string>
#include <string_view>

namespace {
template <size_t Size>
constexpr NsStringView view(const char (&text)[Size]) { return {text, Size - 1}; }

const NsHostApi *hostApi = nullptr;
NsExtensionHandle extensionHandle = nullptr;
VariableExplorerWidget *activeView = nullptr;
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

void *createVariableView(void *, void *nativeParent, NsStringView)
{
    activeView = new VariableExplorerWidget(static_cast<QWidget *>(nativeParent));
    activeView->setPathProvider(&activeDocumentPath);
    if (activeDocumentPath().isEmpty() && !selectedFile.isEmpty())
        activeView->setScriptPath(selectedFile);
    return activeView;
}

void projectChanged(void *, void *, NsStringView) {}

void openVariableExplorer(void *, const NsCommandContext *context)
{
    selectedFile.clear();
    if (context && context->active_file_path.data && context->active_file_path.size > 0)
        selectedFile = QString::fromUtf8(context->active_file_path.data,
                                         qsizetype(context->active_file_path.size));
    const QFileInfo file(selectedFile);
    if (!file.isFile() || (file.suffix().compare(QStringLiteral("py"), Qt::CaseInsensitive) != 0
        && file.suffix().compare(QStringLiteral("pyw"), Qt::CaseInsensitive) != 0)) {
        hostApi->show_message(hostApi->host_context,
            view("Open and save a Python file before using Variable Explorer."), 3500);
        return;
    }
    if (!hostApi->show_view(hostApi->host_context, view("python.variableExplorer"))) return;
    if (activeView) activeView->setScriptPath(selectedFile);
}

int32_t activate(void *, const NsHostApi *host, NsExtensionHandle extension)
{
    const size_t requiredSize = offsetof(NsHostApi, get_active_document_path)
        + sizeof(host->get_active_document_path);
    if (!host || host->abi_version != NS_EXTENSION_ABI_VERSION
        || host->struct_size < requiredSize || !host->register_command
        || !host->register_view || !host->show_view
        || !host->get_active_document_path) return -1;
    hostApi = host;
    extensionHandle = extension;
    const NsSettingSpec settings[]{
        {sizeof(NsSettingSpec), view("python_path"), view("Python interpreter"),
         view("Executable name or absolute path used by the isolated variable explorer runner."),
         NS_SETTING_TEXT, view("python"), 0, 0, nullptr, 0}
    };
    const NsSettingsPageSpec settingsPage{
        sizeof(NsSettingsPageSpec), view("Python Variable Explorer"),
        view("Configure the standalone Python variable inspection extension."),
        view("python interpreter variables inspect runtime"), settings, 1
    };
    const NsCommandSpec command{
        sizeof(NsCommandSpec), view("dev.nativestudio.python_variable_explorer.open"),
        view("Open Variable Explorer"), view("Python"), view("Ctrl+Shift+V"),
        &openVariableExplorer, nullptr
    };
    const NsViewSpec variableView{
        sizeof(NsViewSpec), view("python.variableExplorer"), view("VARIABLE EXPLORER"),
        view("pythonVariableExplorerDock"), NS_VIEW_LOCATION_RIGHT,
        &createVariableView, &projectChanged, nullptr
    };
    if (host->register_settings_page
        && !host->register_settings_page(host->host_context, extension, &settingsPage)) return -1;
    return host->register_view(host->host_context, extension, &variableView)
        && host->register_command(host->host_context, extension, &command) ? 0 : -1;
}

void deactivate(void *)
{
    activeView = nullptr;
    hostApi = nullptr;
    extensionHandle = nullptr;
}

const NsExtensionDescriptor descriptor{
    sizeof(NsExtensionDescriptor), NS_EXTENSION_ABI_VERSION,
    view("dev.nativestudio.python_variable_explorer"), view("Python Variable Explorer"),
    view("1.0.1"), nullptr, &activate, &deactivate
};
}

NS_EXTENSION_EXPORT const NsExtensionDescriptor *ns_extension_entry(void)
{
    return &descriptor;
}
