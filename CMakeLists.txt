cmake_minimum_required(VERSION 3.24)
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

project(NativeStudio VERSION 0.16.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

add_library(nativeide_core STATIC
    src/codeeditor.cpp
    src/codeeditor.h
    src/extension_api.h
    src/extensionmanager.cpp
    src/extensionmanager.h
    src/extensionmanagerdialog.cpp
    src/extensionmanagerdialog.h
    src/gitwidget.cpp
    src/gitwidget.h
    src/keybindingsdialog.cpp
    src/keybindingsdialog.h
    src/languagemanager.cpp
    src/languagemanager.h
    src/languageplugin.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/pluginmanagerdialog.cpp
    src/pluginmanagerdialog.h
    src/projectexplorer.cpp
    src/projectexplorer.h
    src/projectsearchwidget.cpp
    src/projectsearchwidget.h
    src/quickopendialog.cpp
    src/quickopendialog.h
    src/settingsdialog.cpp
    src/settingsdialog.h
    src/terminalwidget.cpp
    src/terminalwidget.h
)

target_link_libraries(nativeide_core PUBLIC Qt6::Widgets Qt6::Network)
target_include_directories(nativeide_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(nativeide_core PRIVATE NATIVE_STUDIO_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(nativeide_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(nativeide_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(NativeStudio WIN32
    src/main.cpp
)
target_link_libraries(NativeStudio PRIVATE nativeide_core)

set_target_properties(NativeStudio PROPERTIES
    OUTPUT_NAME "Native Studio"
)

set(BUNDLED_EXTENSION_BUILD_DIR "${CMAKE_BINARY_DIR}/extensions/bundled")
file(MAKE_DIRECTORY "${BUNDLED_EXTENSION_BUILD_DIR}")

add_library(native_hello_extension MODULE
    extensions/hello/hello_extension.cpp
    src/extension_api.h
)
target_include_directories(native_hello_extension PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(native_hello_extension PROPERTIES
    PREFIX ""
    OUTPUT_NAME "native_hello"
    LIBRARY_OUTPUT_DIRECTORY "${BUNDLED_EXTENSION_BUILD_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${BUNDLED_EXTENSION_BUILD_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BUNDLED_EXTENSION_BUILD_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BUNDLED_EXTENSION_BUILD_DIR}"
)
configure_file(extensions/hello/extension.json
               "${BUNDLED_EXTENSION_BUILD_DIR}/native_hello.json" COPYONLY)

add_library(native_code_tidy_extension MODULE
    extensions/code_tidy/code_tidy_extension.cpp
    src/extension_api.h
)
target_include_directories(native_code_tidy_extension PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(native_code_tidy_extension PROPERTIES
    PREFIX ""
    OUTPUT_NAME "native_code_tidy"
    LIBRARY_OUTPUT_DIRECTORY "${BUNDLED_EXTENSION_BUILD_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${BUNDLED_EXTENSION_BUILD_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BUNDLED_EXTENSION_BUILD_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BUNDLED_EXTENSION_BUILD_DIR}"
)
configure_file(extensions/code_tidy/extension.json
               "${BUNDLED_EXTENSION_BUILD_DIR}/native_code_tidy.json" COPYONLY)

add_library(native_git_extension MODULE
    extensions/git/git_extension.cpp
    src/extension_api.h
)
target_include_directories(native_git_extension PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(native_git_extension PROPERTIES
    PREFIX ""
    OUTPUT_NAME "native_git"
    LIBRARY_OUTPUT_DIRECTORY "${BUNDLED_EXTENSION_BUILD_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${BUNDLED_EXTENSION_BUILD_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BUNDLED_EXTENSION_BUILD_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BUNDLED_EXTENSION_BUILD_DIR}"
)
configure_file(extensions/git/extension.json
               "${BUNDLED_EXTENSION_BUILD_DIR}/native_git.json" COPYONLY)
foreach(extension_target IN ITEMS native_hello_extension native_code_tidy_extension native_git_extension)
    if(MSVC)
        target_compile_options(${extension_target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${extension_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

qt_add_plugin(language_cpp MODULE
    CLASS_NAME CppLanguagePlugin
    plugins/cpp/cpplanguageplugin.cpp
    plugins/cpp/cpplanguageplugin.h
    plugins/cpp/cppplugin.json
    src/cpphighlighter.cpp
    src/cpphighlighter.h
    src/languageplugin.h
)
target_include_directories(language_cpp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(language_cpp PRIVATE Qt6::Widgets)
set_target_properties(language_cpp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/languages"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/languages"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugins/languages"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugins/languages"
)

qt_add_plugin(language_python MODULE
    CLASS_NAME PythonLanguagePlugin
    plugins/python/pythonlanguageplugin.cpp
    plugins/python/pythonlanguageplugin.h
    plugins/python/pythonhighlighter.cpp
    plugins/python/pythonhighlighter.h
    plugins/python/pythonplugin.json
    src/languageplugin.h
)
qt_add_resources(language_python "python_analyzer"
    PREFIX "/native_studio/python"
    BASE "plugins/python"
    FILES plugins/python/analyzer.py
)
target_include_directories(language_python PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/python"
)
target_link_libraries(language_python PRIVATE Qt6::Widgets)
set_target_properties(language_python PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/languages"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/languages"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugins/languages"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugins/languages"
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(nativeide_tests
        tests/editor_tests.cpp
    )
    target_link_libraries(nativeide_tests PRIVATE nativeide_core Qt6::Test)
    add_dependencies(nativeide_tests native_hello_extension native_code_tidy_extension
                     native_git_extension)
    add_test(NAME nativeide_tests COMMAND nativeide_tests)
    set_tests_properties(nativeide_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

install(TARGETS NativeStudio
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
install(TARGETS language_cpp
    LIBRARY DESTINATION bin/plugins/languages
    RUNTIME DESTINATION bin/plugins/languages
)
install(TARGETS language_python
    LIBRARY DESTINATION bin/plugins/languages
    RUNTIME DESTINATION bin/plugins/languages
)
install(TARGETS native_hello_extension
    LIBRARY DESTINATION bin/extensions/bundled
    RUNTIME DESTINATION bin/extensions/bundled
)
install(FILES extensions/hello/extension.json
        DESTINATION bin/extensions/bundled RENAME native_hello.json)
install(TARGETS native_code_tidy_extension
    LIBRARY DESTINATION bin/extensions/bundled
    RUNTIME DESTINATION bin/extensions/bundled
)
install(FILES extensions/code_tidy/extension.json
        DESTINATION bin/extensions/bundled RENAME native_code_tidy.json)
install(TARGETS native_git_extension
    LIBRARY DESTINATION bin/extensions/bundled
    RUNTIME DESTINATION bin/extensions/bundled
)
install(FILES extensions/git/extension.json
        DESTINATION bin/extensions/bundled RENAME native_git.json)
install(FILES src/languageplugin.h src/extension_api.h src/EXTENSION_ABI.md DESTINATION sdk)
