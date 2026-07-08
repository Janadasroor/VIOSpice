/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "api_reference_data.h"

namespace IDE {

static QVector<ApiFunction> s_allFunctions;

static void initFunctions() {
    if (!s_allFunctions.isEmpty()) return;

    auto add = [](const char* name, const char* sig, const char* desc, const char* cat) {
        s_allFunctions.append({QString(name), QString(sig), QString(desc), QString(cat)});
    };

    // Qt Widget API
    add("flux_qt_create_window", "flux_qt_create_window(title) -> handle",
        "Creates a QDialog window. Returns a handle for further operations.", "Qt Widgets");
    add("flux_qt_create_button", "flux_qt_create_button(text) -> handle",
        "Creates a QPushButton with the given text.", "Qt Widgets");
    add("flux_qt_create_label", "flux_qt_create_label(text) -> handle",
        "Creates a QLabel with the given text.", "Qt Widgets");
    add("flux_qt_create_slider", "flux_qt_create_slider(orientation) -> handle",
        "Creates a QSlider. orientation: 0=horizontal, 1=vertical.", "Qt Widgets");
    add("flux_qt_create_lcd", "flux_qt_create_lcd() -> handle",
        "Creates an 8-digit QLCDNumber display.", "Qt Widgets");
    add("flux_qt_create_combobox", "flux_qt_create_combobox() -> handle",
        "Creates a QComboBox dropdown.", "Qt Widgets");
    add("flux_qt_create_lineedit", "flux_qt_create_lineedit(text) -> handle",
        "Creates a QLineEdit text input.", "Qt Widgets");
    add("flux_qt_create_checkbox", "flux_qt_create_checkbox(text) -> handle",
        "Creates a QCheckBox.", "Qt Widgets");
    add("flux_qt_create_spinbox", "flux_qt_create_spinbox() -> handle",
        "Creates a QSpinBox (range 0-1000000).", "Qt Widgets");
    add("flux_qt_create_progressbar", "flux_qt_create_progressbar() -> handle",
        "Creates a QProgressBar (range 0-100).", "Qt Widgets");
    add("flux_qt_create_tableview", "flux_qt_create_tableview(rows, cols) -> handle",
        "Creates a QTableView with the given dimensions.", "Qt Widgets");
    add("flux_qt_create_tabwidget", "flux_qt_create_tabwidget() -> handle",
        "Creates a QTabWidget container.", "Qt Widgets");
    add("flux_qt_create_timer", "flux_qt_create_timer(interval, singleShot) -> handle",
        "Creates a QTimer. Set singleShot=1 for one-shot.", "Qt Widgets");
    add("flux_qt_create_layout", "flux_qt_create_layout(type) -> handle",
        "Creates a layout. type: 'vbox', 'hbox', 'grid'.", "Qt Widgets");
    add("flux_qt_add_widget", "flux_qt_add_widget(container, widget)",
        "Adds a widget to a container.", "Qt Widgets");
    add("flux_qt_set_layout", "flux_qt_set_layout(widget, layout)",
        "Sets a layout on a widget.", "Qt Widgets");
    add("flux_qt_layout_add_widget", "flux_qt_layout_add_widget(layout, widget)",
        "Adds a widget to a layout.", "Qt Widgets");
    add("flux_qt_grid_add_widget", "flux_qt_grid_add_widget(grid, w, row, col, rowSpan, colSpan)",
        "Adds a widget to a grid layout at the specified position.", "Qt Widgets");
    add("flux_qt_set_window_size", "flux_qt_set_window_size(win, w, h)",
        "Sets the size of a window.", "Qt Widgets");
    add("flux_qt_set_text", "flux_qt_set_text(handle, text)",
        "Sets the text property on a widget.", "Qt Widgets");
    add("flux_qt_get_text", "flux_qt_get_text(handle) -> string",
        "Gets the text property from a widget.", "Qt Widgets");
    add("flux_qt_store", "flux_qt_store(name, handle)",
        "Stores a widget handle by name for cross-function access.", "Qt Widgets");
    add("flux_qt_load", "flux_qt_load(name) -> handle",
        "Loads a previously stored widget handle by name.", "Qt Widgets");
    add("flux_qt_on_click_by_name", "flux_qt_on_click_by_name(handle, \"funcName\")",
        "Connects a button's clicked() signal to a FluxScript function.", "Qt Widgets");
    add("flux_qt_on_value_changed_by_name", "flux_qt_on_value_changed_by_name(handle, \"funcName\")",
        "Connects a slider/spinbox's valueChanged(int) signal.", "Qt Widgets");
    add("flux_qt_on_current_index_changed_by_name", "flux_qt_on_current_index_changed_by_name(handle, \"funcName\")",
        "Connects a combo box's currentIndexChanged(int) signal.", "Qt Widgets");
    add("flux_qt_on_toggled_by_name", "flux_qt_on_toggled_by_name(handle, \"funcName\")",
        "Connects a checkbox's toggled(bool) signal.", "Qt Widgets");
    add("flux_qt_msg_box", "flux_qt_msg_box(title, text)",
        "Shows an information message box.", "Qt Widgets");
    add("flux_qt_combo_add_item", "flux_qt_combo_add_item(handle, text)",
        "Adds an item to a combo box.", "Qt Widgets");
    add("flux_qt_table_set_value", "flux_qt_table_set_value(table, row, col, value)",
        "Sets a numeric value in a table cell.", "Qt Widgets");
    add("flux_qt_table_set_header", "flux_qt_table_set_header(table, col, text)",
        "Sets a column header in a table.", "Qt Widgets");
    add("flux_qt_lcd_display", "flux_qt_lcd_display(handle, value)",
        "Displays a value on an LCD widget.", "Qt Widgets");

    // Workspace API
    add("viora_flux_print", "viora_flux_print(msg)",
        "Prints a message to the console with [STDOUT] prefix.", "Workspace");
    add("flux_get_var", "flux_get_var(name) -> value",
        "Gets a global variable from the bridge store.", "Workspace");
    add("flux_set_var", "flux_set_var(name, value)",
        "Sets a global variable in the bridge store.", "Workspace");
    add("flux_set_prop", "flux_set_prop(ref, prop, value)",
        "Sets a numeric property on a schematic component.", "Workspace");
    add("flux_set_prop_str", "flux_set_prop_str(ref, prop, value)",
        "Sets a string property on a schematic component.", "Workspace");
    add("flux_run_sim", "flux_run_sim(type, tStop, tStep)",
        "Runs a SPICE analysis. type: 'tran', 'ac', 'dc', 'op', 'live'.", "Workspace");
    add("flux_get_project_name", "flux_get_project_name() -> string",
        "Returns the current project name.", "Workspace");
    add("flux_get_schematic_file", "flux_get_schematic_file() -> string",
        "Returns the current schematic file path.", "Workspace");
    add("flux_select_schematic", "flux_select_schematic(fileName)",
        "Switches the active schematic tab.", "Workspace");

    // Simulation API
    add("flux_sim_get_vector_size", "flux_sim_get_vector_size(name) -> int",
        "Returns the number of samples in a simulation result vector.", "Simulation");
    add("flux_sim_get_vector_val", "flux_sim_get_vector_val(name, index) -> float",
        "Returns the Y-axis value of a vector at the given index.", "Simulation");
    add("flux_sim_get_vector_x", "flux_sim_get_vector_x(name, index) -> float",
        "Returns the X-axis value (time/freq) of a vector at the given index.", "Simulation");
    add("flux_plot_point", "flux_plot_point(series, x, y)",
        "Adds a point to the waveform viewer.", "Simulation");
    add("flux_to_str", "flux_to_str(val) -> string",
        "Converts a number to a string handle.", "Simulation");
    add("flux_concat", "flux_concat(s1, s2) -> string",
        "Concatenates two string handles.", "Simulation");

    // Design Rule API
    add("flux_erc_get_component_count", "flux_erc_get_component_count() -> int",
        "Returns the number of components in the schematic.", "Design Rules");
    add("flux_erc_get_ref", "flux_erc_get_ref(index) -> string",
        "Returns the reference name of a component.", "Design Rules");
    add("flux_erc_get_value", "flux_erc_get_value(index) -> string",
        "Returns the value of a component.", "Design Rules");
    add("flux_erc_get_type", "flux_erc_get_type(index) -> string",
        "Returns the type of a component.", "Design Rules");
    add("flux_erc_get_pin_count", "flux_erc_get_pin_count(compIndex) -> int",
        "Returns the number of pins on a component.", "Design Rules");
    add("flux_erc_get_pin_net", "flux_erc_get_pin_net(compIndex, pinIndex) -> string",
        "Returns the net name at a component pin.", "Design Rules");
    add("flux_erc_report", "flux_erc_report(severity, msg, compIndex)",
        "Reports an ERC violation.", "Design Rules");

    // Math
    add("sin", "sin(x) -> float", "Sine function.", "Math");
    add("cos", "cos(x) -> float", "Cosine function.", "Math");
    add("tan", "tan(x) -> float", "Tangent function.", "Math");
    add("asin", "asin(x) -> float", "Arc sine function.", "Math");
    add("acos", "acos(x) -> float", "Arc cosine function.", "Math");
    add("atan", "atan(x) -> float", "Arc tangent function.", "Math");
    add("sqrt", "sqrt(x) -> float", "Square root.", "Math");
    add("pow", "pow(base, exp) -> float", "Power function.", "Math");
    add("exp", "exp(x) -> float", "Exponential (e^x).", "Math");
    add("log", "log(x) -> float", "Natural logarithm.", "Math");
    add("log10", "log10(x) -> float", "Base-10 logarithm.", "Math");
    add("abs", "abs(x) -> float", "Absolute value.", "Math");
    add("floor", "floor(x) -> int", "Floor (round down).", "Math");
    add("ceil", "ceil(x) -> int", "Ceiling (round up).", "Math");
    add("min", "min(a, b) -> float", "Minimum of two values.", "Math");
    add("max", "max(a, b) -> float", "Maximum of two values.", "Math");
    add("pi", "pi = 3.14159265...", "Pi constant.", "Math");
}

const QVector<ApiFunction>& ApiReferenceData::allFunctions() {
    initFunctions();
    return s_allFunctions;
}

QVector<ApiFunction> ApiReferenceData::byCategory(const QString& category) {
    initFunctions();
    QVector<ApiFunction> result;
    for (const auto& fn : s_allFunctions) {
        if (fn.category == category)
            result.append(fn);
    }
    return result;
}

QVector<ApiFunction> ApiReferenceData::search(const QString& query) {
    initFunctions();
    QVector<ApiFunction> result;
    QString lower = query.toLower();
    for (const auto& fn : s_allFunctions) {
        if (fn.name.toLower().contains(lower) ||
            fn.description.toLower().contains(lower) ||
            fn.category.toLower().contains(lower)) {
            result.append(fn);
        }
    }
    return result;
}

} // namespace IDE
