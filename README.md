# Native Studio

A fully native, language-neutral desktop IDE shell built with C++20, Qt 6 Widgets, and CMake. It provides a tabbed text editor, UTF-8 atomic saves, unsaved-change protection, a VS Code-style workspace explorer, and dynamically loadable language plugins.

## Language plugins

Language support is not compiled into the editor core. Native Qt plugins are discovered from `plugins/languages` beside the executable. A plugin implements `LanguagePlugin`, advertises its file extensions, and creates a `QSyntaxHighlighter` for matching documents. C++ and Python support are included as independently deployed plugins; other languages can be installed without changing the core.

Use **Plugins > Rescan Language Plugins** after copying a new plugin into that directory.

Use **Plugins > Plugin Explorer** to browse and search language plugins published on the [Native Studio GitHub releases page](https://github.com/AnFa222/native-studio/releases). The explorer automatically checks the latest public release and shows which plugins are available, installed, or ready to update. Installation, removal, and updates are safely applied after restarting because Windows cannot replace a loaded DLL.

## Language intelligence

Completion and diagnostics are supplied by each language plugin rather than the editor core. The included Python and C++ plugins provide language keywords, built-ins and symbols found in the current document. Suggestions open from the first typed character, remain filtered as the prefix grows, and appear as inline ghost text plus a native popup. Press **Tab** or **Enter** to accept the selected suggestion, use the arrow keys to change it, or press **Ctrl+Space** to request the list.

Both plugins also provide live syntax diagnostics. Errors and warnings use colored squiggles and line-number markers, show details on hover, and appear in **View > Problems**. The Python provider checks delimiters, strings, required colons, and mixed indentation. The C++ provider checks delimiters, strings, comments, and preprocessor conditionals.

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
