# Native Studio

A fully native, language-neutral desktop IDE shell built with C++20, Qt 6 Widgets, and CMake. It provides a tabbed text editor, UTF-8 atomic saves, unsaved-change protection, a VS Code-style workspace explorer, and dynamically loadable language plugins.

The main window is shown before optional services start. Language plugins and native extensions are then loaded incrementally in interleaved event-loop jobs, while the terminal shell starts asynchronously. This keeps painting and input responsive without moving Qt plugin construction or extension UI callbacks onto unsafe worker threads. The previous window size, maximized/fullscreen state, and dock layout are restored on launch; a first launch opens maximized.

## Language plugins

Language support is not compiled into the editor core. Native Qt plugins are discovered from `plugins/languages` beside the executable. A plugin implements `LanguagePlugin`, advertises its file extensions, and creates a `QSyntaxHighlighter` for matching documents. C++ and Python support are included as independently deployed plugins; other languages can be installed without changing the core.

Use **Plugins > Rescan Language Plugins** after copying a new plugin into that directory.

Use **Plugins > Plugin Explorer** to browse and search language plugins published on the [Native Studio GitHub releases page](https://github.com/AnFa222/native-studio/releases). The explorer automatically checks the latest public release and shows which plugins are available, installed, or ready to update. Installation, removal, and updates are safely applied after restarting because Windows cannot replace a loaded DLL.

## Native extensions

General-purpose extensions use the versioned C ABI in `sdk/extension_api.h`. They are ordinary native shared libraries loaded in-process from `bin/extensions` or the per-user extensions directory. Commands and host services use direct function pointers and UTF-8 string views—there is no JSON or RPC marshalling in the call path, no JavaScript runtime, and no Qt requirement for extension binaries.

The v1 host API supports activation and cleanup, command registration, active-file/project context, status messages, logging, opening files, inserting or replacing editor text, and launching processes in the integrated terminal. A compiled `native_hello` example is bundled under **Extensions > Examples**. Use **Extensions > Manage Native Extensions** to import JSON manifests, enable or disable extensions, and remove user-installed binaries. See [src/EXTENSION_ABI.md](src/EXTENSION_ABI.md) for the manifest schema, ABI contract, and a minimal CMake project.

## Language intelligence

Completion and diagnostics are supplied by each language plugin rather than the editor core. The included Python and C++ plugins provide language keywords, built-ins and symbols found in the current document. Suggestions open from the first typed character, remain filtered as the prefix grows, and appear in a native popup. Press **Tab** or **Enter** to accept the selected suggestion, use the arrow keys to change it, or press **Ctrl+Space** to request the list.

Both plugins also provide live diagnostics. Errors and warnings use colored squiggles and line-number markers, show details on hover, and appear in **View > Problems**. Python Language Support 2.0 embeds a persistent standard-library AST analyzer that uses the installed Python compiler for authoritative syntax errors, then checks undefined and unused names, stray literals, unreachable code, mutable defaults, duplicate dictionary keys, unsafe comparisons, wildcard imports, and other semantic issues. It falls back to the native syntax checker if Python is unavailable. The C++ provider checks common statement errors, delimiters, strings, comments, and preprocessor conditionals.

## Running and terminal

The detected language plugin can provide a native run configuration. The Python plugin locates an installed Python 3 interpreter and runs `.py`, `.pyw`, and `.pyi` files with it; the same interpreter powers full AST diagnostics. Choose **Run > Run Current File** or press **Ctrl+F5**. Output appears in the integrated terminal.

The bottom terminal is also an interactive system shell. Type directly into its output surface—there is no separate command box. Input is routed to a running program first, so Python `input()` works, and the **Terminate** control stops the active program or shell. Opening a project changes its working directory to the workspace root. It can be shown or hidden from **View > Terminal**.

## Explorer

The explorer shows all files and hidden items in the opened workspace. Its header offers New File, New Folder, Refresh, and Collapse All. Right-click any item to create, rename, or delete entries.

## Build

Requirements: CMake 3.24+, Qt 6.5+ (`Widgets` and `Test`), and a C++20 compiler.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run `build\Release\Native Studio.exe` after building. Use **Project > Open Project** to populate the explorer, then double-click any text file to edit it.
