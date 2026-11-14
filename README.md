# Native Studio

A fully native, language-neutral desktop IDE shell built with C++20, Qt 6 Widgets, and CMake. It provides a tabbed text editor, UTF-8 atomic saves, unsaved-change protection, a VS Code-style workspace explorer, and dynamically loadable language plugins.

## Language plugins

Language support is not compiled into the editor core. Native Qt plugins are discovered from `plugins/languages` beside the executable. A plugin implements `LanguagePlugin`, advertises its file extensions, and creates a `QSyntaxHighlighter` for matching documents. C++ and Python support are included as independently deployed plugins; other languages can be installed without changing the core.

Use **Plugins > Rescan Language Plugins** after copying a new plugin into that directory.

Use **Plugins > Manage Plugins** to inspect installed plugins, enter a GitHub `owner/repository`, check its latest release, and stage `language_*.dll` assets for installation or update. Removing and updating plugins is applied safely on restart because Windows cannot replace a loaded DLL. Public repositories need no credentials. For a private plugin repository, start the IDE with `NATIVE_STUDIO_GITHUB_TOKEN` set to a GitHub token that can read that repository; Native Studio does not persist the token.

## Running and terminal

The detected language plugin can provide a native run configuration. The Python plugin runs `.py`, `.pyw`, and `.pyi` files with the platform Python launcher. Choose **Run > Run Current File** or press **Ctrl+F5**. Output appears in the integrated terminal.

The bottom terminal is also an interactive system shell. Opening a project changes its working directory to the workspace root. It can be shown or hidden from **View > Terminal**.

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
