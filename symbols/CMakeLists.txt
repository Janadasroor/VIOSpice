set(VIOSYMBOLS_SOURCES ${SYMBOLS_SOURCES})
list(TRANSFORM VIOSYMBOLS_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/../")

list(APPEND VIOSYMBOLS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/symbol_canvas.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/symbol_canvas.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/symbol_tool_manager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/symbol_tool_manager.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/symbol_properties_panel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/symbol_properties_panel.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/symbol_pin_table_panel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/symbol_pin_table_panel.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/symbol_wizard_panel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor/symbol_wizard_panel.h"
)

add_library(VioSymbols STATIC ${VIOSYMBOLS_SOURCES})
vioraeda_configure_module_target(VioSymbols)
target_link_libraries(VioSymbols PUBLIC VioCore)
