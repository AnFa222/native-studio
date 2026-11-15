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
  "permissions": ["ui.commands", "editor.write"],
  "binary": {
    "windows": "example.dll",
    "linux": "example.so",
    "macos": "example.dylib"
  }
}
```

`binary` may also be one string for a single-platform package. It must be a relative path inside the manifest directory. The manager validates the schema, ABI version, binary type, containment, and exported descriptor identity/version before activation. Updates and removals are staged until restart when Windows has the DLL loaded.

`permissions` is an optional array of host capabilities. Native Studio displays requested permissions before importing an extension and rejects unknown permission names. Host calls are denied unless the extension declares the corresponding capability:

- `ui.commands`: register commands in the Extensions menu.
- `editor.read`: read the active document.
- `editor.write`: insert or replace active-document text.
- `editor.events`: subscribe to editor lifecycle events.
- `filesystem.read`: ask the host to open a file.
- `process.execute`: launch programs in the integrated terminal.
- `settings`: register a settings page and read its values.
- `ui.views`: register and open lazy native dock views.

Permissions authorize host API calls only. Native shared libraries execute in-process, so install binaries only from publishers you trust.

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
- `register_settings_page`: describe a native settings page with boolean, integer, text, or choice fields. Native Studio owns and renders the Qt controls; the extension remains independent of Qt.
- `get_extension_setting`: copy one setting's current UTF-8 value into a caller-owned buffer; call with a null buffer first to query its byte size. Values are scoped automatically to the calling extension.
- `show_view`: open a registered view by stable identifier. Views remain lazy and are created only when requested.
- `register_view`: register a persistent native Qt dock view, including its stable ID, title, dock object name, default location, lazy widget factory, and optional project-change callback.

Command callbacks receive the active file path, project root, and document-modified state without an additional host query.

## Native dock views

`register_view` lets an extension contribute a fully native sidebar or bottom panel. Native Studio owns the dock container, saves its position, tab group, visibility, floating state, and size, and destroys the view before unloading the extension. The factory runs only when the view is first opened or restored from the previous session.

The factory receives an opaque `native_parent` that is a `QWidget*` and returns the created `QWidget*` as `void*`. This optional part of the API therefore requires the extension to use the same Qt major version and compatible C++ runtime as Native Studio. Extensions that only use commands, settings, events, and editor services remain Qt-independent.

```cpp
#include "extension_api.h"
#include <QLabel>

static void *createView(void *, void *parent, NsStringView projectRoot)
{
    auto *label = new QLabel(static_cast<QWidget *>(parent));
    label->setText(projectRoot.size ? "Project view" : "Open a folder");
    return label;
}

static void projectChanged(void *, void *nativeView, NsStringView projectRoot)
{
    auto *label = static_cast<QLabel *>(nativeView);
    label->setText(projectRoot.size ? "Project changed" : "Open a folder");
}

const NsViewSpec viewSpec{
    sizeof(NsViewSpec), sv("com.example.projectView"), sv("PROJECT VIEW"),
    sv("exampleProjectDock"), NS_VIEW_LOCATION_LEFT,
    &createView, &projectChanged, nullptr
};

api->register_view(api->host_context, self, &viewSpec);
api->show_view(api->host_context, sv("com.example.projectView"));
```

View IDs and dock object names must be stable and unique. Use a publisher-prefixed view ID. Do not retain the `project_root` string view after a callback returns. UI callbacks execute on the main thread; perform slow work asynchronously and marshal results back to the UI thread.

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

## Extension settings pages

An activated extension may register one settings page. The specification is copied during registration, so temporary arrays are safe. Setting keys must be unique within the page. Stored values live under an extension-specific namespace and are never shared with another extension. The host presents the page in **Settings > Preferences**, participates in settings search, and commits changes when the user selects **OK**.

```cpp
static const NsSettingChoice styleChoices[] = {
    {sizeof(NsSettingChoice), sv("conventional"), sv("Conventional")},
    {sizeof(NsSettingChoice), sv("compact"), sv("Compact")}
};

static const NsSettingSpec settings[] = {
    {sizeof(NsSettingSpec), sv("format_on_save"), sv("Format on save"),
     sv("Format supported files immediately before saving."),
     NS_SETTING_BOOLEAN, sv("true"), 0, 0, nullptr, 0},
    {sizeof(NsSettingSpec), sv("indent_width"), sv("Indent width"),
     sv("Spaces used for one indentation level."),
     NS_SETTING_INTEGER, sv("4"), 2, 8, nullptr, 0},
    {sizeof(NsSettingSpec), sv("style"), sv("Formatting style"),
     sv("Select the formatter's spacing style."),
     NS_SETTING_CHOICE, sv("conventional"), 0, 0,
     styleChoices, sizeof(styleChoices) / sizeof(styleChoices[0])}
};

static const NsSettingsPageSpec page{
    sizeof(NsSettingsPageSpec), sv("Example Formatter"),
    sv("Configure formatting supplied by this extension."),
    sv("format indentation style"), settings,
    sizeof(settings) / sizeof(settings[0])
};

api->register_settings_page(api->host_context, self, &page);
```

Read a setting when it is needed so changes take effect without reloading the extension:

```cpp
const size_t bytes = api->get_extension_setting(
    api->host_context, self, sv("indent_width"), nullptr, 0);
std::string value(bytes, '\0');
api->get_extension_setting(
    api->host_context, self, sv("indent_width"), value.data(), value.size());
```

`get_extension_setting` returns the exact UTF-8 byte count and does not append a null terminator. Boolean values are `true` or `false`; integer values use decimal notation; choices return their declared value. Unknown keys return zero. As with all append-only v1 additions, check `NsHostApi::struct_size` before calling these functions when the extension must also run on an older Native Studio build.
