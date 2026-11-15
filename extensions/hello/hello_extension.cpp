#include "extension_api.h"

#include <cstddef>

namespace {
template <size_t Size>
constexpr NsStringView view(const char (&text)[Size])
{
    return {text, Size - 1};
}

const NsHostApi *hostApi = nullptr;

void sayHello(void *, const NsCommandContext *context)
{
    if (!hostApi) return;
    if (context && context->active_file_path.size > 0) {
        hostApi->show_message(hostApi->host_context,
            view("Hello from a native extension — active file context received."), 4000);
    } else {
        hostApi->show_message(hostApi->host_context,
            view("Hello from a native extension."), 4000);
    }
}

void insertHello(void *, const NsCommandContext *)
{
    if (hostApi && hostApi->insert_text)
        hostApi->insert_text(hostApi->host_context, view("Hello from Native Hello"));
}

int32_t activate(void *, const NsHostApi *host, NsExtensionHandle extension)
{
    if (!host || host->abi_version != NS_EXTENSION_ABI_VERSION
        || host->struct_size < sizeof(NsHostApi) || !host->register_command) return -1;
    hostApi = host;
    const NsCommandSpec helloCommand{
        sizeof(NsCommandSpec),
        view("dev.nativestudio.hello.say_hello"),
        view("Say Hello"),
        view("Examples"),
        view("Ctrl+Alt+H"),
        &sayHello,
        nullptr
    };
    const NsCommandSpec insertCommand{
        sizeof(NsCommandSpec),
        view("dev.nativestudio.hello.insert_text"),
        view("Insert Hello Text"),
        view("Examples"),
        {nullptr, 0},
        &insertHello,
        nullptr
    };
    host->log(host->host_context, NS_LOG_INFO, view("Activating native hello extension"));
    if (!host->register_command(host->host_context, extension, &helloCommand)) return -1;
    return host->register_command(host->host_context, extension, &insertCommand) ? 0 : -1;
}

void deactivate(void *)
{
    hostApi = nullptr;
}

const NsExtensionDescriptor descriptor{
    sizeof(NsExtensionDescriptor),
    NS_EXTENSION_ABI_VERSION,
    view("dev.nativestudio.hello"),
    view("Native Hello"),
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
