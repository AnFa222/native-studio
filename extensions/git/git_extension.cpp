#include "extension_api.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace {
template <size_t Size>
constexpr NsStringView view(const char (&text)[Size])
{
    return {text, Size - 1};
}

const NsHostApi *hostApi = nullptr;
NsExtensionHandle extensionHandle = nullptr;

NsStringView view(std::string_view text)
{
    return {text.data(), text.size()};
}

std::string setting(std::string_view key, std::string_view fallback)
{
    if (!hostApi || !hostApi->get_extension_setting || !extensionHandle)
        return std::string(fallback);
    const size_t required = hostApi->get_extension_setting(
        hostApi->host_context, extensionHandle, view(key), nullptr, 0);
    if (required == 0) return std::string(fallback);
    std::string result(required, '\0');
    hostApi->get_extension_setting(hostApi->host_context, extensionHandle, view(key),
                                   result.data(), result.size());
    return result;
}

void openSourceControl(void *, const NsCommandContext *)
{
    if (!hostApi || !hostApi->show_view) return;
    if (!hostApi->show_view(hostApi->host_context, view("git.sourceControl"))) {
        hostApi->show_message(hostApi->host_context,
                              view("Source Control could not be opened."), 3500);
    }
}

void signInToGitHub(void *, const NsCommandContext *)
{
    if (!hostApi || !hostApi->run_process) return;
    const std::string program = setting("git_path", "git");
    const NsStringView arguments[]{
        view("credential-manager"), view("github"), view("login")
    };
    hostApi->run_process(hostApi->host_context, view(program), arguments,
                         sizeof(arguments) / sizeof(arguments[0]), {nullptr, 0});
}

void runGitRemoteCommand(const NsCommandContext *context, std::string_view command)
{
    if (!hostApi || !hostApi->run_process || !context || context->project_root.size == 0) {
        if (hostApi) hostApi->show_message(hostApi->host_context,
            view("Open a project folder before running a Git command."), 3500);
        return;
    }
    const std::string program = setting("git_path", "git");
    const std::string root(context->project_root.data, context->project_root.size);
    const std::string commandText(command);
    const NsStringView arguments[]{view("-C"), view(root), view(commandText), view("--ff-only")};
    const size_t argumentCount = command == "pull" ? 4 : 3;
    hostApi->run_process(hostApi->host_context, view(program), arguments,
                         argumentCount, view(root));
}

void pull(void *, const NsCommandContext *context)
{
    runGitRemoteCommand(context, "pull");
}

void push(void *, const NsCommandContext *context)
{
    runGitRemoteCommand(context, "push");
}

int32_t activate(void *, const NsHostApi *host, NsExtensionHandle extension)
{
    const size_t requiredSize = offsetof(NsHostApi, show_view) + sizeof(host->show_view);
    if (!host || host->abi_version != NS_EXTENSION_ABI_VERSION
        || host->struct_size < requiredSize || !host->register_command || !host->show_view) {
        return -1;
    }
    hostApi = host;
    extensionHandle = extension;
    const NsSettingSpec settings[]{
        {sizeof(NsSettingSpec), view("git_path"), view("Git executable"),
         view("Executable name or absolute path used for Git and GitHub authentication."),
         NS_SETTING_TEXT, view("git"), 0, 0, nullptr, 0}
    };
    const NsSettingsPageSpec settingsPage{
        sizeof(NsSettingsPageSpec), view("Git"),
        view("Configure the bundled native Git integration."),
        view("git github source control credential manager login pull push"), settings, 1
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
    const NsCommandSpec signInCommand{
        sizeof(NsCommandSpec), view("dev.nativestudio.git.sign_in_github"),
        view("Sign in to GitHub"), view("Git"), {nullptr, 0}, &signInToGitHub, nullptr
    };
    const NsCommandSpec pullCommand{
        sizeof(NsCommandSpec), view("dev.nativestudio.git.pull"),
        view("Pull"), view("Git"), {nullptr, 0}, &pull, nullptr
    };
    const NsCommandSpec pushCommand{
        sizeof(NsCommandSpec), view("dev.nativestudio.git.push"),
        view("Push"), view("Git"), {nullptr, 0}, &push, nullptr
    };
    host->log(host->host_context, NS_LOG_INFO, view("Activated bundled Git extension"));
    if (host->register_settings_page
        && !host->register_settings_page(host->host_context, extension, &settingsPage)) return -1;
    return host->register_command(host->host_context, extension, &command)
        && host->register_command(host->host_context, extension, &signInCommand)
        && host->register_command(host->host_context, extension, &pullCommand)
        && host->register_command(host->host_context, extension, &pushCommand) ? 0 : -1;
}

void deactivate(void *)
{
    hostApi = nullptr;
    extensionHandle = nullptr;
}

const NsExtensionDescriptor descriptor{
    sizeof(NsExtensionDescriptor),
    NS_EXTENSION_ABI_VERSION,
    view("dev.nativestudio.git"),
    view("Git"),
    view("1.2.0"),
    nullptr,
    &activate,
    &deactivate
};
}

NS_EXTENSION_EXPORT const NsExtensionDescriptor *ns_extension_entry(void)
{
    return &descriptor;
}
