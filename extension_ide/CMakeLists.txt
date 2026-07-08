# =============================================================================
# Extension IDE Module - Full IDE for FluxScript extension development
# =============================================================================

set(EXTENSION_IDE_SOURCES
    # Main window
    extension_ide_window.cpp
    extension_ide_window.h

    # Editor
    editor/ide_editor.cpp
    editor/ide_editor.h
    editor/ide_highlighter.cpp
    editor/ide_highlighter.h
    editor/ide_completer.cpp
    editor/ide_completer.h
    editor/ide_tab_widget.cpp
    editor/ide_tab_widget.h
    editor/ide_find_replace.cpp
    editor/ide_find_replace.h

    # Panels
    panels/file_tree_panel.cpp
    panels/file_tree_panel.h
    panels/api_reference_panel.cpp
    panels/api_reference_panel.h
    panels/output_panel.cpp
    panels/output_panel.h
    panels/manifest_editor_panel.cpp
    panels/manifest_editor_panel.h
    panels/template_browser_panel.cpp
    panels/template_browser_panel.h
    panels/extension_scaffold_dialog.cpp
    panels/extension_scaffold_dialog.h

    # Core
    core/extension_runner.cpp
    core/extension_runner.h
    core/extension_ide_state.cpp
    core/extension_ide_state.h
    core/api_reference_data.cpp
    core/api_reference_data.h

    # Resources
    resources/extension_ide.qrc
)

qt_add_library(VioExtensionIDE STATIC ${EXTENSION_IDE_SOURCES})

target_include_directories(VioExtensionIDE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)

target_link_libraries(VioExtensionIDE PUBLIC
    VioCore
    Qt6::Widgets
    Qt6::Svg
)
