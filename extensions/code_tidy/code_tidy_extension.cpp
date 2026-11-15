#include "extension_api.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {
template <size_t Size>
constexpr NsStringView view(const char (&text)[Size])
{
    return {text, Size - 1};
}

const NsHostApi *hostApi = nullptr;

std::string tidyCode(std::string_view source)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < source.size()) {
        size_t end = start;
        while (end < source.size() && source[end] != '\n' && source[end] != '\r') ++end;
        std::string line;
        line.reserve(end - start + 4);
        size_t column = 0;
        size_t index = start;
        while (index < end && (source[index] == ' ' || source[index] == '\t')) {
            if (source[index] == '\t') {
                const size_t spaces = 4 - (column % 4);
                line.append(spaces, ' ');
                column += spaces;
            } else {
                line.push_back(' ');
                ++column;
            }
            ++index;
        }
        line.append(source.substr(index, end - index));
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        lines.push_back(std::move(line));
        if (end < source.size() && source[end] == '\r' && end + 1 < source.size()
            && source[end + 1] == '\n') {
            start = end + 2;
        } else {
            start = end + 1;
        }
    }

    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (lines.empty()) return {};
    std::string result;
    for (const auto &line : lines) {
        result += line;
        result.push_back('\n');
    }
    return result;
}

bool replaceIfChanged(std::string_view source)
{
    if (!hostApi || !hostApi->replace_document_text) return false;
    const std::string formatted = tidyCode(source);
    if (formatted == source) return false;
    const NsStringView replacement{formatted.data(), formatted.size()};
    return hostApi->replace_document_text(hostApi->host_context, replacement) != 0;
}

void formatDocument(void *, const NsCommandContext *)
{
    if (!hostApi || !hostApi->get_active_document_text) return;
    const size_t required = hostApi->get_active_document_text(hostApi->host_context, nullptr, 0);
    std::string source(required, '\0');
    if (required > 0)
        hostApi->get_active_document_text(hostApi->host_context, source.data(), source.size());
    if (replaceIfChanged(source)) {
        hostApi->show_message(hostApi->host_context, view("Code Tidy formatted the document."), 2500);
    } else {
        hostApi->show_message(hostApi->host_context, view("Code Tidy: already clean."), 2000);
    }
}

void onHostEvent(void *, const NsEventContext *context)
{
    if (!context || context->struct_size < sizeof(NsEventContext)
        || context->event_type != NS_EVENT_DOCUMENT_WILL_SAVE) return;
    const std::string_view source(context->document_text.data
            ? context->document_text.data : "",
        context->document_text.data ? context->document_text.size : 0);
    if (replaceIfChanged(source) && hostApi && hostApi->show_message)
        hostApi->show_message(hostApi->host_context, view("Code Tidy formatted on save."), 1500);
}

int32_t activate(void *, const NsHostApi *host, NsExtensionHandle extension)
{
    const size_t requiredSize = offsetof(NsHostApi, get_active_document_text)
        + sizeof(host->get_active_document_text);
    if (!host || host->abi_version != NS_EXTENSION_ABI_VERSION
        || host->struct_size < requiredSize || !host->register_command
        || !host->register_event_listener || !host->get_active_document_text) return -1;
    hostApi = host;
    const NsCommandSpec command{
        sizeof(NsCommandSpec),
        view("dev.nativestudio.code_tidy.format_document"),
        view("Format Document"),
        view("Code Tidy"),
        view("Ctrl+Shift+I"),
        &formatDocument,
        nullptr
    };
    if (!host->register_command(host->host_context, extension, &command)) return -1;
    if (!host->register_event_listener(host->host_context, extension,
                                       NS_EVENT_DOCUMENT_WILL_SAVE, &onHostEvent, nullptr)) return -1;
    host->log(host->host_context, NS_LOG_INFO, view("Activated bundled Code Tidy extension"));
    return 0;
}

void deactivate(void *)
{
    hostApi = nullptr;
}

const NsExtensionDescriptor descriptor{
    sizeof(NsExtensionDescriptor),
    NS_EXTENSION_ABI_VERSION,
    view("dev.nativestudio.code_tidy"),
    view("Code Tidy"),
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
