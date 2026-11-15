#include "extension_api.h"

#include <cstddef>

namespace {
template <size_t Size>
constexpr NsStringView view(const char (&text)[Size])
{
    return {text, Size - 1};
}

const NsHostApi *hostApi = nullptr;

void openSourceControl(void *, const NsCommandContext *)
{
    if (!hostApi || !hostApi->show_view) return;
    if (!hostApi->show_view(hostApi->host_context, view("git.sourceControl"))) {
        hostApi->show_message(hostApi->host_context,
                              view("Source Control could not be opened."), 3500);
    }
}

int32_t activate(void *, const NsHostApi *host, NsExtensionHandle extension)
{
    const size_t requiredSize = offsetof(NsHostApi, show_view) + sizeof(host->show_view);
    if (!host || host->abi_version != NS_EXTENSION_ABI_VERSION
        || host->struct_size < requiredSize || !host->register_command || !host->show_view) {
        return -1;
    }
    hostApi = host;
    const NsSettingSpec settings[]{
        {sizeof(NsSettingSpec), view("git_path"), view("Git executable"),
         view("Executable name or absolute path used for Git commands."),
         NS_SETTING_TEXT, view("git"), 0, 0, nullptr, 0}
    };
    const NsSettingsPageSpec settingsPage{
        sizeof(NsSettingsPageSpec), view("Git"),
        view("Configure the bundled native Git integration."),
        view("git source control executable path"), settings, 1
    };
    const NsCommandSpec command{
        sizeof(NsCommandSpec),
        view("dev.nativestudio.git.open_source_control"),
        view("Open Source Control"),
        view("Git"),
        view("Ctrl+Shift+G"),
        &openSourceControl,
        nullptr
    };
    host->log(host->host_context, NS_LOG_INFO, view("Activated bundled Git extension"));
    if (host->register_settings_page
        && !host->register_settings_page(host->host_context, extension, &settingsPage)) return -1;
    return host->register_command(host->host_context, extension, &command) ? 0 : -1;
}

void deactivate(void *)
{
    hostApi = nullptr;
}

const NsExtensionDescriptor descriptor{
    sizeof(NsExtensionDescriptor),
    NS_EXTENSION_ABI_VERSION,
    view("dev.nativestudio.git"),
    view("Git"),
    view("1.0.0"),
    nullptr,
    &activate,
    &deactivate
};
}

NS_EXTENSION_EXPORT const NsExtensionDescriptor *ns_extension_entry(void)
{
    return &descriptor;
}
