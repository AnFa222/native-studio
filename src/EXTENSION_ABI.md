# Native Studio extension ABI

`extension_api.h` defines the stable, low-overhead ABI for general-purpose Native Studio extensions. Extensions are in-process native shared libraries (`.dll`, `.so`, or `.dylib`) and do not need Qt.

## Performance and compatibility

- The operating-system loader loads each binary once.
- Command dispatch is a direct native function-pointer call.
- Host services are direct function-pointer calls in the opposite direction.
- Strings are non-owning UTF-8 pointer/length views.
- There is no IPC, JSON marshalling in the call path, reflection, JavaScript, or C++ STL across the boundary. JSON is read only when discovering an extension manifest.

The ABI uses C-compatible structs so an extension can use any C++20 implementation without sharing Qt or standard-library objects with the host. Every extensible struct begins with `struct_size`; every descriptor and host table declares an ABI version. Native Studio implements `NS_EXTENSION_ABI_VERSION == 1`. The v1 host table is append-only: extensions must check `NsHostApi::struct_size` before using fields added after the initial release.

Never throw exceptions across the ABI boundary. The module that allocates memory must release it. String views, command contexts, and event contexts are valid only during the receiving call. Activation, command, and editor-event callbacks run on the UI thread. Long-running work belongs on extension-owned worker threads, which must stop during `deactivate`.

Host integer functions return `1` for success and `0` for failure. An extension's `activate` callback returns `0` for success.

## JSON manifest

Every extension ships a JSON manifest beside its binary:

```json
{
  "manifestVersion": 1,
  "id": "com.example.extension",
  "name": "Example Extension",
  "version": "1.0.0",
  "abiVersion": 1,
  "description": "What the extension does.",
  "author": "Example Author",
  "binary": {
    "windows": "example.dll",
    "linux": "example.so",
    "macos": "example.dylib"
  }
}
```

`binary` may also be one string for a single-platform package. It must be a relative path inside the manifest directory. The manager validates the schema, ABI version, binary type, containment, and exported descriptor identity/version before activation. Updates and removals are staged until restart when Windows has the DLL loaded.

## Entry point

Export exactly one entry function:

```cpp
NS_EXTENSION_EXPORT const NsExtensionDescriptor *ns_extension_entry(void);
```

The returned descriptor must remain valid until the library is unloaded. Its `id` and `version` must exactly match the JSON manifest.

## Minimal C++ extension

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
    sv("com.example.extension"), sv("Example Extension"), sv("1.0.0"),
    nullptr, &activate, &deactivate
};

NS_EXTENSION_EXPORT const NsExtensionDescriptor *ns_extension_entry(void)
{
    return &descriptor;
}
```

## CMake

```cmake
cmake_minimum_required(VERSION 3.24)
project(example_extension LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
add_library(example_extension MODULE extension.cpp)
target_include_directories(example_extension PRIVATE "path/to/NativeStudio/sdk")
set_target_properties(example_extension PROPERTIES PREFIX "")
```

Import the manifest from **Extensions > Manage Native Extensions**. The manager copies the binary and normalized manifest into the per-user extension directory.

Extensions shipped with Native Studio are installed under `bin/extensions/bundled`. They may be disabled, but cannot be updated or removed through the manager. Imported extensions remain isolated in the per-user extension directory.

## Host API v1

- `register_command`: register an action under Extensions, optionally grouped by category.
- `show_message`: display a status-bar message.
- `log`: write a debug, info, warning, or error message.
- `open_file`: open a path in an editor tab.
- `insert_text`: insert UTF-8 text at the active cursor or replace its selection.
- `replace_document_text`: replace the active document as one undoable edit.
- `run_process`: launch a program and argument array in the integrated terminal.
- `register_event_listener`: subscribe to a supported editor event with a direct callback. v1 currently exposes `NS_EVENT_DOCUMENT_WILL_SAVE`.
- `get_active_document_text`: copy the active UTF-8 document into a caller-owned buffer; call with a null buffer first to query its byte size.

Command callbacks receive the active file path, project root, and document-modified state without an additional host query.

## Format-on-save event

Register `NS_EVENT_DOCUMENT_WILL_SAVE` during activation to receive the active path and document text immediately before the editor writes the file. The callback may call `replace_document_text`; the replacement is then saved by the host. Event delivery is synchronous and uses direct function calls.

```cpp
static void beforeSave(void *, const NsEventContext *event)
{
    if (!event || event->event_type != NS_EVENT_DOCUMENT_WILL_SAVE) return;
    // Inspect event->document_text and call host->replace_document_text when needed.
}

api->register_event_listener(api->host_context, self,
    NS_EVENT_DOCUMENT_WILL_SAVE, &beforeSave, nullptr);
```
