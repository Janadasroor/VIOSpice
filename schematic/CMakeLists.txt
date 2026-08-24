set(VIOSCHEMATIC_SOURCES "")
foreach(src ${SCHEMATIC_SOURCES})
    get_filename_component(abs_src "${CMAKE_CURRENT_SOURCE_DIR}/../${src}" ABSOLUTE)
    list(APPEND VIOSCHEMATIC_SOURCES ${abs_src})
endforeach()

add_library(VioSchematic STATIC ${VIOSCHEMATIC_SOURCES})
set_target_properties(VioSchematic PROPERTIES AUTOMOC ON)
vioraeda_configure_module_target(VioSchematic)

target_include_directories(VioSchematic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/items
    ${CMAKE_CURRENT_SOURCE_DIR}/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/io
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/factories
)

target_link_libraries(VioSchematic PUBLIC VioCore VioSymbols VioSimulator FluxScriptManager VioUI Qt${QT_VERSION_MAJOR}::Concurrent)
target_include_directories(VioSchematic PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/../simulator/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../simulator/bridge
)

# New Phase 2 Components
target_sources(VioSchematic PRIVATE
    ui/logic_editor_panel.cpp
    ui/logic_editor_panel.h
    ui/schematic_minimap.cpp
    ui/schematic_minimap.h
    ui/simulation/simulation_generator_panel.cpp
    ui/simulation/simulation_generator_panel.h
    ui/simulation/simulation_design_explorer_panel.cpp
    ui/simulation/simulation_design_explorer_panel.h
    ui/simulation/simulation_analyzer.cpp
    ui/simulation/simulation_analyzer.h
    ui/smart_probe_overlay.cpp
    ui/smart_probe_overlay.h
    ui/smart_probe_engine.cpp
    ui/smart_probe_engine.h
    ui/smith_chart_widget.cpp
    ui/smith_chart_widget.h
    ui/new_extension_dialog.cpp
    ui/new_extension_dialog.h
    ui/manifest_editor.cpp
    ui/manifest_editor.h
    items/virtual_terminal_item.cpp
    items/virtual_terminal_item.h
    items/logic_toggle_item.cpp
    items/logic_toggle_item.h
    items/logic_probe_item.cpp
    items/logic_probe_item.h
    items/rotary_knob_item.cpp
    items/rotary_knob_item.h
    items/joystick_item.cpp
    items/joystick_item.h
    ui/virtual_terminal_window.cpp
    ui/virtual_terminal_window.h
    dialogs/virtual_terminal_properties_dialog.cpp
    dialogs/virtual_terminal_properties_dialog.h
    analysis/passes/component_extractor.cpp
    analysis/passes/component_extractor.h
    analysis/passes/connectivity_evaluator.cpp
    analysis/passes/connectivity_evaluator.h
    analysis/passes/component_formatter.cpp
    analysis/passes/component_formatter.h
    analysis/passes/model_injector.cpp
    analysis/passes/model_injector.h
    analysis/passes/xspice_block_translator.cpp
    analysis/passes/xspice_block_translator.h
    analysis/passes/netlist_formatter.cpp
    analysis/passes/netlist_formatter.h
    analysis/passes/spice_compat_rewriter.cpp
    analysis/passes/spice_compat_rewriter.h
    models/schematic_schema.h
)

if(VIOSPICE_ENGINE_FOUND)
    target_include_directories(VioSchematic PRIVATE ${VIOSPICE_ENGINE_INCLUDE_DIRS})
endif()

# For backward compatibility if other modules use VioSchematicCore or VioSchematicUI
add_library(VioSchematicCore ALIAS VioSchematic)
add_library(VioSchematicUI ALIAS VioSchematic)

if(BUILD_TESTING)
    vioraeda_add_test(test_spice_directive_netlist
        SOURCES tests/test_spice_directive_netlist.cpp
        LINK_LIBS VioSchematic VioSymbols VioSimulator VioCore VioUI VioVioraIDE
    )
    target_include_directories(test_spice_directive_netlist PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/items
    )

    vioraeda_add_test(test_virtual_terminal
        SOURCES tests/test_virtual_terminal.cpp
        LINK_LIBS VioSchematic VioSymbols VioSimulator VioCore VioUI VioVioraIDE
    )
    target_include_directories(test_virtual_terminal PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/items
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
    )
endif()
