# Native extension SDK

Native Studio extensions are in-process shared libraries (`.dll`, `.so`, or `.dylib`). The ABI is C-compatible so extensions can be implemented in C++20 without depending on Qt or matching the host's C++ standard-library ABI.

## Performance model

- The host loads binaries once with the operating-system dynamic loader.
- Commands call extension functions directly through one function pointer.
- Host services are direct function-pointer calls in the opposite direction.
- Strings cross the boundary as non-owning UTF-8 pointer/length views.
- There is no IPC, JSON, reflection, virtual dispatch, or scripting runtime.

The cost of a command dispatch is effectively one Qt signal in the UI followed by one native indirect call.

## ABI rules

Include `extension_api.h` and export `ns_extension_entry`. Set every `struct_size` field to `sizeof(the_struct)` and require `NS_EXTENSION_ABI_VERSION`. Never throw exceptions across the ABI boundary. Memory is owned and released by the module that allocated it. `NsStringView` and command-context pointers are valid only for the duration of the receiving call.

Activation and command callbacks run on the UI thread. Long work should run on an extension-owned worker thread; UI-facing host calls must be marshalled back to the UI thread. Stop workers during `deactivate` before the library is unloaded.

`register_command` and the other integer host functions return `1` for success and `0` for failure. An extension's `activate` callback returns `0` for success.

## Minimal extension

```cpp
#include "extension_api.h"

#include <cstddef>

template <size_t N>
constexpr NsStringView sv(const char (&text)[N]) { return {text, N - 1}; }

static const NsHostApi *host = nullptr;

static void run(void *, const NsCommandContext *context)
{
    if (context && context->active_file_path.size)
        host->show_message(host->host_context, sv("An active file is open"), 3000);
    host->insert_text(host->host_context, sv("native extension text"));
}

static int32_t activate(void *, const NsHostApi *api, NsExtensionHandle self)
{
    if (!api || api->abi_version != NS_EXTENSION_ABI_VERSION) return -1;
    host = api;
    const NsCommandSpec command{
        sizeof(NsCommandSpec), sv("com.example.insert"), sv("Insert text"),
        sv("Example"), {nullptr, 0}, &run, nullptr
    };
    return api->register_command(api->host_context, self, &command) ? 0 : -1;
}

static void deactivate(void *) { host = nullptr; }

static const NsExtensionDescriptor descriptor{
    sizeof(NsExtensionDescriptor), NS_EXTENSION_ABI_VERSION,
    sv("com.example.extension"), sv("Example extension"), sv("1.0.0"),
    nullptr, &activate, &deactivate
};

NS_EXTENSION_EXPORT const NsExtensionDescriptor *ns_extension_entry(void)
{
    return &descriptor;
}
```

## Minimal CMake project

```cmake
cmake_minimum_required(VERSION 3.24)
project(example_extension LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

add_library(example_extension MODULE extension.cpp)
target_include_directories(example_extension PRIVATE "path/to/NativeStudio/sdk")
set_target_properties(example_extension PROPERTIES PREFIX "")
```

Copy the resulting library into `bin/extensions` beside Native Studio, or into the user extensions directory reported by `ExtensionManager::userExtensionDirectory()` when building against the application source.

## Host API v1

- `register_command`: add an action under the Extensions menu, optionally grouped by category.
- `show_message`: display a status-bar message.
- `log`: write a debug, info, warning, or error message.
- `open_file`: open a path in an editor tab.
- `insert_text`: insert UTF-8 text at the active cursor or selection.
- `replace_document_text`: replace the active document as one undoable edit.
- `run_process`: launch a program with an argument array in the integrated terminal.

Command callbacks receive the active file path, project root, and modified state without an extra host query.
