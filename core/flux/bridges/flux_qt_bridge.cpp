/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flux_qt_bridge.h"
#include "../engine/flux_script_engine.h"
#include <flux/jit_engine.h>
#include <QMetaProperty>
#include <QDebug>
#include <cstring>
#include <string>
#include <vector>

// SPICE runtime functions (defined in fluxscript spice_runtime.cpp / flux_runtime.cpp)
extern "C" void flux_set_parameter(double, double);
extern "C" double flux_get_parameter(double);
extern "C" double flux_get_voltage(double);
extern "C" double flux_get_current(double);
extern "C" double flux_register_analysis(double);
extern "C" double flux_register_measure(double, double);
extern "C" double flux_register_probe(double, double);
extern "C" double flux_register_save(double);
extern "C" double flux_config_get(double, double);
extern "C" void flux_config_set(double, double);
extern "C" double flux_config_get_str(double, double);
extern "C" void flux_config_set_str(double, double);
extern "C" double flux_has_permission(double);
extern "C" double flux_extension_id();
extern "C" void flux_on_event(double);
extern "C" void flux_on_event_all();
extern "C" void flux_emit_event(double);
extern "C" void flux_emit_event_data(double, double);

// Helper to cast between void* and double handles
template <typename To, typename From>
inline To bit_cast(const From& src) noexcept {
    static_assert(sizeof(To) == sizeof(From), "bit_cast sizes must match");
    To dst;
    std::memcpy(&dst, &src, sizeof(To));
    return dst;
}

static const char* dbl_to_str(double d) {
    uint64_t raw;
    std::memcpy(&raw, &d, sizeof(raw));
    return reinterpret_cast<const char*>(static_cast<uintptr_t>(raw));
}

// Forward declarations for workspace bridge functions
extern "C" void viora_flux_print(double);
extern "C" void flux_print_num(double);

extern "C" double flux_qt_create_panel(double);
extern "C" double flux_qt_create_form_row(double, double);
extern "C" double flux_qt_create_button_bar(double, double);
extern "C" double flux_qt_create_separator();
extern "C" double flux_qt_create_group(double);
extern "C" void flux_qt_set_placeholder(double, double);
extern "C" void flux_qt_set_tooltip(double, double);
extern "C" void flux_qt_set_enabled(double, double);
extern "C" void flux_qt_set_fixed_size(double, double, double);
extern "C" double flux_qt_get_value(double);
extern "C" void flux_qt_set_value(double, double);
extern "C" void flux_qt_set_range(double, double, double);
extern "C" void flux_qt_set_stylesheet(double, double);
extern "C" void flux_qt_connect(double, double, double);
extern "C" void flux_qt_add_widget_smart(double, double);
extern "C" double flux_qt_adopt(double);
extern "C" double flux_qt_list_widgets(double);
extern "C" void flux_qt_embed(double, double);
extern "C" double flux_qt_get_widget_info(double, double);
extern "C" double flux_qt_create_scope();
extern "C" double flux_qt_create_waveform_viewer();
extern "C" double flux_qt_create_scope_dock();
extern "C" double flux_qt_create_oscilloscope(double);

// Smart defaults & shorthand
extern "C" double flux_qt_create_panel(double);
extern "C" double flux_qt_create_form_row(double, double);
extern "C" double flux_qt_create_button_bar(double, double);
extern "C" double flux_qt_create_separator();
extern "C" double flux_qt_create_group(double);
extern "C" void flux_qt_set_placeholder(double, double);
extern "C" void flux_qt_set_tooltip(double, double);
extern "C" void flux_qt_set_enabled(double, double);
extern "C" void flux_qt_set_fixed_size(double, double, double);
extern "C" double flux_qt_get_value(double);
extern "C" void flux_qt_set_value(double, double);
extern "C" void flux_qt_set_range(double, double, double);
extern "C" void flux_qt_set_stylesheet(double, double);
extern "C" void flux_qt_connect(double, double, double);
extern "C" void flux_qt_add_widget(double, double);
extern "C" double flux_get_var(double);
extern "C" void flux_set_var(double, double);
extern "C" void flux_set_prop(double, double, double);
extern "C" void flux_set_prop_str(double, double, double);
extern "C" int flux_sim_get_vector_size(double);
extern "C" double flux_sim_get_vector_val(double, int);
extern "C" double flux_sim_get_vector_x(double, int);
extern "C" void flux_run_sim(double, double, double);
extern "C" double flux_get_project_name();
extern "C" void flux_plot_point(double, double, double);
extern "C" double flux_to_str(double);
extern "C" double flux_concat(double, double);

FluxQtBridge& FluxQtBridge::instance() {
    static FluxQtBridge inst;
    return inst;
}

FluxQtBridge::FluxQtBridge(QObject* parent) : QObject(parent) {}

double FluxQtBridge::registerObject(QObject* obj) {
    if (!obj) return 0.0;
    std::lock_guard<std::mutex> lock(m_mutex);
    void* ptr = static_cast<void*>(obj);
    m_registry[ptr] = obj;
    return bit_cast<double>(ptr);
}

QObject* FluxQtBridge::resolveHandle(double handle) const {
    void* ptr = bit_cast<void*>(handle);
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_registry.find(ptr);
    if (it != m_registry.end()) {
        if (it.value()) return it.value().data();
        // Object was deleted but not unregistered
        const_cast<FluxQtBridge*>(this)->m_registry.erase(it);
    }
    return nullptr;
}

double FluxQtBridge::getProperty(double handle, const char* name) {
    QObject* obj = instance().resolveHandle(handle);
    if (!obj) return 0.0;

    QVariant val = obj->property(name);
    if (val.userType() == QMetaType::Double || val.userType() == QMetaType::Int) {
        return val.toDouble();
    } else if (val.userType() == QMetaType::QString) {
        // FluxScript strings are also double-handles to const char*
        QString s = val.toString();
        static thread_local std::vector<std::string> s_propertyPool;
        s_propertyPool.push_back(s.toStdString());
        return bit_cast<double>(s_propertyPool.back().c_str()); 
    }
    return 0.0;
}

void FluxQtBridge::setProperty(double handle, const char* name, double value) {
    QObject* obj = instance().resolveHandle(handle);
    if (!obj) return;

    // Check if the property expects a string handle (bitcast back)
    int propIdx = obj->metaObject()->indexOfProperty(name);
    if (propIdx >= 0) {
        QMetaProperty prop = obj->metaObject()->property(propIdx);
        if (prop.userType() == QMetaType::QString) {
            const char* s = static_cast<const char*>(bit_cast<void*>(value));
            obj->setProperty(name, QString::fromUtf8(s));
            return;
        }
    }

    obj->setProperty(name, value);
}

void FluxQtBridge::connectSignal(double handle, const char* signal, double functionHandle) {
    QObject* obj = resolveHandle(handle);
    if (!obj) return;

    this->QObject::setProperty("last_func_handle", functionHandle);
    QObject::connect(obj, signal, this, SLOT(onBridgeEvent()));
}

void FluxQtBridge::connectSignalByName(double handle, const char* signal, const char* functionName) {
    QObject* obj = resolveHandle(handle);
    if (!obj || !functionName) return;

    m_signalNameMap[QPointer<QObject>(obj)] = QString::fromUtf8(functionName);
    QObject::connect(obj, signal, this, SLOT(onBridgeEvent()));
}

void FluxQtBridge::onBridgeEvent() {
    QObject* sdr = sender();
    if (!sdr) return;

    // Prefer named function lookup
    auto it = m_signalNameMap.find(QPointer<QObject>(sdr));
    if (it != m_signalNameMap.end()) {
        QString error;
        FluxScriptEngine::instance().callFunction(
            it.value().toUtf8().constData(), {}, &error);
        return;
    }

    // Fallback: old generic call (deprecated but kept for compatibility)
    QString error;
    FluxScriptEngine::instance().callFunction("", {}, &error);
}

// C-API wrappers for property bridge (registered as JIT symbols below)
extern "C" {
    double flux_qt_get_property(double handle, double name_dbl) {
        return FluxQtBridge::instance().getProperty(handle, dbl_to_str(name_dbl));
    }
    void flux_qt_set_property(double handle, double name_dbl, double value) {
        FluxQtBridge::instance().setProperty(handle, dbl_to_str(name_dbl), value);
    }
}

// Forward declarations for C-API bridge functions defined in flux_qt_widgets.cpp
extern "C" {
    double flux_qt_create_window(double);
    double flux_qt_create_button(double);
    double flux_qt_create_slider(double);
    double flux_qt_create_lcd();
    double flux_qt_create_label(double);
    double flux_qt_create_combobox();
    double flux_qt_create_lineedit(double);
    double flux_qt_create_checkbox(double);
    double flux_qt_create_spinbox();
    double flux_qt_create_progressbar();
    double flux_qt_create_tableview(double, double);
    void flux_qt_add_widget(double, double);
    void flux_qt_msg_box(double, double);
    void flux_qt_on_click(double, double);
    void flux_qt_on_value_changed(double, double);
    void flux_qt_on_current_index_changed(double, double);
    void flux_qt_on_toggled(double, double);
    void flux_qt_lcd_display(double, double);
    void flux_qt_on_click_by_name(double, double);
    void flux_qt_on_value_changed_by_name(double, double);
    void flux_qt_on_current_index_changed_by_name(double, double);
    void flux_qt_on_toggled_by_name(double, double);
    void flux_qt_set_window_size(double, double, double);
    double flux_qt_create_layout(double);
    void flux_qt_set_layout(double, double);
    double flux_qt_create_timer(double, double);
    void flux_qt_timer_start(double);
    void flux_qt_timer_stop(double);
    void flux_qt_layout_add_widget(double, double);
    void flux_qt_grid_add_widget(double, double, double, double, double, double);
    void flux_qt_combo_add_item(double, double);
    void flux_qt_combo_clear(double);
    void flux_qt_combo_set_current_index(double, double);
    void flux_qt_table_set_item(double, double, double, double);
    void flux_qt_table_set_value(double, double, double, double);
    void flux_qt_table_set_header(double, double, double);
    double flux_qt_table_row_count(double);
    double flux_qt_table_col_count(double);
    void flux_qt_store(double, double);
    double flux_qt_load(double);
    double flux_qt_create_tabwidget();
    void flux_qt_tab_add(double, double, double);
    double flux_qt_create_widget();
    void flux_qt_set_text(double, double);
    double flux_qt_get_text(double);
    // Workspace bridge
    void viora_flux_print(double);
    double flux_get_var(double);
    void flux_set_var(double, double);
    void flux_set_prop(double, double, double);
    void flux_set_prop_str(double, double, double);
    int flux_sim_get_vector_size(double);
    double flux_sim_get_vector_val(double, int);
    double flux_sim_get_vector_x(double, int);
    void flux_run_sim(double, double, double);
    double flux_get_project_name();
    void flux_plot_point(double, double, double);
}

void registerQtBridgeJitSymbols(Flux::FluxJIT& jit) {
    // Qt Widget creation functions (from flux_qt_widgets.cpp)
    jit.registerFunction("flux_qt_create_window", (void*)&flux_qt_create_window);
    jit.registerFunction("flux_qt_create_button", (void*)&flux_qt_create_button);
    jit.registerFunction("flux_qt_create_slider", (void*)&flux_qt_create_slider);
    jit.registerFunction("flux_qt_create_lcd", (void*)&flux_qt_create_lcd);
    jit.registerFunction("flux_qt_create_label", (void*)&flux_qt_create_label);
    jit.registerFunction("flux_qt_create_combobox", (void*)&flux_qt_create_combobox);
    jit.registerFunction("flux_qt_create_lineedit", (void*)&flux_qt_create_lineedit);
    jit.registerFunction("flux_qt_create_checkbox", (void*)&flux_qt_create_checkbox);
    jit.registerFunction("flux_qt_create_spinbox", (void*)&flux_qt_create_spinbox);
    jit.registerFunction("flux_qt_create_progressbar", (void*)&flux_qt_create_progressbar);
    jit.registerFunction("flux_qt_create_tableview", (void*)&flux_qt_create_tableview);
    jit.registerFunction("flux_qt_add_widget", (void*)&flux_qt_add_widget);
    jit.registerFunction("flux_qt_msg_box", (void*)&flux_qt_msg_box);
    jit.registerFunction("flux_qt_on_click", (void*)&flux_qt_on_click);
    jit.registerFunction("flux_qt_on_value_changed", (void*)&flux_qt_on_value_changed);
    jit.registerFunction("flux_qt_on_current_index_changed", (void*)&flux_qt_on_current_index_changed);
    jit.registerFunction("flux_qt_on_toggled", (void*)&flux_qt_on_toggled);
    jit.registerFunction("flux_qt_lcd_display", (void*)&flux_qt_lcd_display);
    jit.registerFunction("flux_qt_set_window_size", (void*)&flux_qt_set_window_size);
    jit.registerFunction("flux_qt_create_layout", (void*)&flux_qt_create_layout);
    jit.registerFunction("flux_qt_set_layout", (void*)&flux_qt_set_layout);
    jit.registerFunction("flux_qt_create_timer", (void*)&flux_qt_create_timer);
    jit.registerFunction("flux_qt_timer_start", (void*)&flux_qt_timer_start);
    jit.registerFunction("flux_qt_timer_stop", (void*)&flux_qt_timer_stop);
    jit.registerFunction("flux_qt_layout_add_widget", (void*)&flux_qt_layout_add_widget);
    jit.registerFunction("flux_qt_grid_add_widget", (void*)&flux_qt_grid_add_widget);
    jit.registerFunction("flux_qt_on_click_by_name", (void*)&flux_qt_on_click_by_name);
    jit.registerFunction("flux_qt_on_value_changed_by_name", (void*)&flux_qt_on_value_changed_by_name);
    jit.registerFunction("flux_qt_on_current_index_changed_by_name", (void*)&flux_qt_on_current_index_changed_by_name);
    jit.registerFunction("flux_qt_on_toggled_by_name", (void*)&flux_qt_on_toggled_by_name);
    jit.registerFunction("flux_qt_combo_add_item", (void*)&flux_qt_combo_add_item);
    jit.registerFunction("flux_qt_combo_clear", (void*)&flux_qt_combo_clear);
    jit.registerFunction("flux_qt_combo_set_current_index", (void*)&flux_qt_combo_set_current_index);
    jit.registerFunction("flux_qt_table_set_item", (void*)&flux_qt_table_set_item);
    jit.registerFunction("flux_qt_table_set_value", (void*)&flux_qt_table_set_value);
    jit.registerFunction("flux_qt_table_set_header", (void*)&flux_qt_table_set_header);
    jit.registerFunction("flux_qt_table_row_count", (void*)&flux_qt_table_row_count);
    jit.registerFunction("flux_qt_table_col_count", (void*)&flux_qt_table_col_count);

    // Widget name store (used by extensions for cross-function widget access)
    jit.registerFunction("flux_qt_store", (void*)&flux_qt_store);
    jit.registerFunction("flux_qt_load", (void*)&flux_qt_load);

    // Tab and container widgets
    jit.registerFunction("flux_qt_create_tabwidget", (void*)&flux_qt_create_tabwidget);
    jit.registerFunction("flux_qt_tab_add", (void*)&flux_qt_tab_add);
    jit.registerFunction("flux_qt_create_widget", (void*)&flux_qt_create_widget);

    // Text field accessors
    jit.registerFunction("flux_qt_set_text", (void*)&flux_qt_set_text);
    jit.registerFunction("flux_qt_get_text", (void*)&flux_qt_get_text);

    // Property bridge (used by extension templates)
    jit.registerFunction("flux_qt_get_property", (void*)&flux_qt_get_property);
    jit.registerFunction("flux_qt_set_property", (void*)&flux_qt_set_property);

    // viora_flux_print - templates reference it by this exact name
    jit.registerFunction("viora_flux_print", (void*)&viora_flux_print);

    // Widget adoption (also register here for JITContextManager instance)
    jit.registerFunction("flux_qt_adopt", (void*)&flux_qt_adopt);
    jit.registerFunction("flux_qt_list_widgets", (void*)&flux_qt_list_widgets);
    jit.registerFunction("flux_qt_embed", (void*)&flux_qt_embed);
    jit.registerFunction("flux_qt_get_widget_info", (void*)&flux_qt_get_widget_info);

    // Smart defaults
    jit.registerFunction("flux_qt_create_panel", (void*)&flux_qt_create_panel);
    jit.registerFunction("flux_qt_create_form_row", (void*)&flux_qt_create_form_row);
    jit.registerFunction("flux_qt_create_button_bar", (void*)&flux_qt_create_button_bar);
    jit.registerFunction("flux_qt_create_separator", (void*)&flux_qt_create_separator);
    jit.registerFunction("flux_qt_create_group", (void*)&flux_qt_create_group);
    jit.registerFunction("flux_qt_set_placeholder", (void*)&flux_qt_set_placeholder);
    jit.registerFunction("flux_qt_set_tooltip", (void*)&flux_qt_set_tooltip);
    jit.registerFunction("flux_qt_set_enabled", (void*)&flux_qt_set_enabled);
    jit.registerFunction("flux_qt_set_fixed_size", (void*)&flux_qt_set_fixed_size);
    jit.registerFunction("flux_qt_get_value", (void*)&flux_qt_get_value);
    jit.registerFunction("flux_qt_set_value", (void*)&flux_qt_set_value);
    jit.registerFunction("flux_qt_set_range", (void*)&flux_qt_set_range);
    jit.registerFunction("flux_qt_set_stylesheet", (void*)&flux_qt_set_stylesheet);
    jit.registerFunction("flux_qt_connect", (void*)&flux_qt_connect);
    jit.registerFunction("flux_qt_add_widget_smart", (void*)&flux_qt_add_widget_smart);

    // Simulation widget factories
    jit.registerFunction("flux_qt_create_scope", (void*)&flux_qt_create_scope);
    jit.registerFunction("flux_qt_create_waveform_viewer", (void*)&flux_qt_create_waveform_viewer);
    jit.registerFunction("flux_qt_create_scope_dock", (void*)&flux_qt_create_scope_dock);
    jit.registerFunction("flux_qt_create_oscilloscope", (void*)&flux_qt_create_oscilloscope);
}

void register_flux_qt_jit_symbols() {
    auto& jit = Flux::JITEngine::instance();
    fprintf(stderr, "[Bridge] register_flux_qt_jit_symbols called, initialized=%d\n", jit.isInitialized());
    if (!jit.isInitialized()) {
        fprintf(stderr, "[Bridge] JIT not initialized, skipping registration\n");
        return;
    }

    jit.registerFunction("flux_qt_create_window", (void*)&flux_qt_create_window);
    jit.registerFunction("flux_qt_create_button", (void*)&flux_qt_create_button);
    jit.registerFunction("flux_qt_create_slider", (void*)&flux_qt_create_slider);
    jit.registerFunction("flux_qt_create_lcd", (void*)&flux_qt_create_lcd);
    jit.registerFunction("flux_qt_create_label", (void*)&flux_qt_create_label);
    jit.registerFunction("flux_qt_create_combobox", (void*)&flux_qt_create_combobox);
    jit.registerFunction("flux_qt_create_lineedit", (void*)&flux_qt_create_lineedit);
    jit.registerFunction("flux_qt_create_checkbox", (void*)&flux_qt_create_checkbox);
    jit.registerFunction("flux_qt_create_spinbox", (void*)&flux_qt_create_spinbox);
    jit.registerFunction("flux_qt_create_progressbar", (void*)&flux_qt_create_progressbar);
    jit.registerFunction("flux_qt_create_tableview", (void*)&flux_qt_create_tableview);
    jit.registerFunction("flux_qt_add_widget", (void*)&flux_qt_add_widget);
    jit.registerFunction("flux_qt_msg_box", (void*)&flux_qt_msg_box);
    jit.registerFunction("flux_qt_on_click", (void*)&flux_qt_on_click);
    jit.registerFunction("flux_qt_on_value_changed", (void*)&flux_qt_on_value_changed);
    jit.registerFunction("flux_qt_on_current_index_changed", (void*)&flux_qt_on_current_index_changed);
    jit.registerFunction("flux_qt_on_toggled", (void*)&flux_qt_on_toggled);
    jit.registerFunction("flux_qt_lcd_display", (void*)&flux_qt_lcd_display);
    jit.registerFunction("flux_qt_set_window_size", (void*)&flux_qt_set_window_size);
    jit.registerFunction("flux_qt_create_layout", (void*)&flux_qt_create_layout);
    jit.registerFunction("flux_qt_set_layout", (void*)&flux_qt_set_layout);
    jit.registerFunction("flux_qt_create_timer", (void*)&flux_qt_create_timer);
    jit.registerFunction("flux_qt_timer_start", (void*)&flux_qt_timer_start);
    jit.registerFunction("flux_qt_timer_stop", (void*)&flux_qt_timer_stop);
    jit.registerFunction("flux_qt_layout_add_widget", (void*)&flux_qt_layout_add_widget);
    jit.registerFunction("flux_qt_grid_add_widget", (void*)&flux_qt_grid_add_widget);
    jit.registerFunction("flux_qt_on_click_by_name", (void*)&flux_qt_on_click_by_name);
    jit.registerFunction("flux_qt_on_value_changed_by_name", (void*)&flux_qt_on_value_changed_by_name);
    jit.registerFunction("flux_qt_on_current_index_changed_by_name", (void*)&flux_qt_on_current_index_changed_by_name);
    jit.registerFunction("flux_qt_on_toggled_by_name", (void*)&flux_qt_on_toggled_by_name);
    jit.registerFunction("flux_qt_combo_add_item", (void*)&flux_qt_combo_add_item);
    jit.registerFunction("flux_qt_combo_clear", (void*)&flux_qt_combo_clear);
    jit.registerFunction("flux_qt_combo_set_current_index", (void*)&flux_qt_combo_set_current_index);
    jit.registerFunction("flux_qt_table_set_item", (void*)&flux_qt_table_set_item);
    jit.registerFunction("flux_qt_table_set_value", (void*)&flux_qt_table_set_value);
    jit.registerFunction("flux_qt_table_set_header", (void*)&flux_qt_table_set_header);
    jit.registerFunction("flux_qt_table_row_count", (void*)&flux_qt_table_row_count);
    jit.registerFunction("flux_qt_table_col_count", (void*)&flux_qt_table_col_count);
    jit.registerFunction("viora_flux_print", (void*)&viora_flux_print);
    jit.registerFunction("flux_print_num", (void*)&flux_print_num);
    jit.registerFunction("flux_get_var", (void*)&flux_get_var);
    jit.registerFunction("flux_set_var", (void*)&flux_set_var);
    jit.registerFunction("flux_set_prop", (void*)&flux_set_prop);
    jit.registerFunction("flux_set_prop_str", (void*)&flux_set_prop_str);
    jit.registerFunction("flux_qt_store", (void*)&flux_qt_store);
    jit.registerFunction("flux_qt_load", (void*)&flux_qt_load);
    jit.registerFunction("flux_qt_create_tabwidget", (void*)&flux_qt_create_tabwidget);
    jit.registerFunction("flux_qt_tab_add", (void*)&flux_qt_tab_add);
    jit.registerFunction("flux_qt_create_widget", (void*)&flux_qt_create_widget);
    jit.registerFunction("flux_qt_set_text", (void*)&flux_qt_set_text);
    jit.registerFunction("flux_qt_get_text", (void*)&flux_qt_get_text);
    jit.registerFunction("flux_qt_get_property", (void*)&flux_qt_get_property);
    jit.registerFunction("flux_qt_set_property", (void*)&flux_qt_set_property);

    // Smart defaults
    jit.registerFunction("flux_qt_create_panel", (void*)&flux_qt_create_panel);
    jit.registerFunction("flux_qt_create_form_row", (void*)&flux_qt_create_form_row);
    jit.registerFunction("flux_qt_create_button_bar", (void*)&flux_qt_create_button_bar);
    jit.registerFunction("flux_qt_create_separator", (void*)&flux_qt_create_separator);
    jit.registerFunction("flux_qt_create_group", (void*)&flux_qt_create_group);
    jit.registerFunction("flux_qt_set_placeholder", (void*)&flux_qt_set_placeholder);
    jit.registerFunction("flux_qt_set_tooltip", (void*)&flux_qt_set_tooltip);
    jit.registerFunction("flux_qt_set_enabled", (void*)&flux_qt_set_enabled);
    jit.registerFunction("flux_qt_set_fixed_size", (void*)&flux_qt_set_fixed_size);
    jit.registerFunction("flux_qt_get_value", (void*)&flux_qt_get_value);
    jit.registerFunction("flux_qt_set_value", (void*)&flux_qt_set_value);
    jit.registerFunction("flux_qt_set_range", (void*)&flux_qt_set_range);
    jit.registerFunction("flux_qt_set_stylesheet", (void*)&flux_qt_set_stylesheet);
    fprintf(stderr, "[Bridge] After set_stylesheet\n");
    jit.registerFunction("flux_qt_connect", (void*)&flux_qt_connect);
    fprintf(stderr, "[Bridge] Before add_widget_smart\n");
    jit.registerFunction("flux_qt_add_widget_smart", (void*)&flux_qt_add_widget_smart);
    jit.registerFunction("flux_qt_create_scope", (void*)&flux_qt_create_scope);
    jit.registerFunction("flux_qt_create_waveform_viewer", (void*)&flux_qt_create_waveform_viewer);
    jit.registerFunction("flux_qt_create_scope_dock", (void*)&flux_qt_create_scope_dock);
    jit.registerFunction("flux_qt_create_oscilloscope", (void*)&flux_qt_create_oscilloscope);

    fprintf(stderr, "[Bridge] About to register adoption functions, add_widget_smart=%p adopt=%p\n",
            (void*)&flux_qt_add_widget_smart, (void*)&flux_qt_adopt);

    // Widget adoption
    jit.registerFunction("flux_qt_adopt", (void*)&flux_qt_adopt);
    jit.registerFunction("flux_qt_list_widgets", (void*)&flux_qt_list_widgets);
    jit.registerFunction("flux_qt_embed", (void*)&flux_qt_embed);
    jit.registerFunction("flux_qt_get_widget_info", (void*)&flux_qt_get_widget_info);

    fprintf(stderr, "[Bridge] Adopt functions registered\n");
    qDebug() << "[Bridge] Registered adopt=" << (void*)&flux_qt_adopt
             << "list=" << (void*)&flux_qt_list_widgets
             << "embed=" << (void*)&flux_qt_embed
             << "info=" << (void*)&flux_qt_get_widget_info;

    // Smart defaults & shorthand
    jit.registerFunction("flux_qt_create_panel", (void*)&flux_qt_create_panel);
    jit.registerFunction("flux_qt_create_form_row", (void*)&flux_qt_create_form_row);
    jit.registerFunction("flux_qt_create_button_bar", (void*)&flux_qt_create_button_bar);
    jit.registerFunction("flux_qt_create_separator", (void*)&flux_qt_create_separator);
    jit.registerFunction("flux_qt_create_group", (void*)&flux_qt_create_group);
    jit.registerFunction("flux_qt_set_placeholder", (void*)&flux_qt_set_placeholder);
    jit.registerFunction("flux_qt_set_tooltip", (void*)&flux_qt_set_tooltip);
    jit.registerFunction("flux_qt_set_enabled", (void*)&flux_qt_set_enabled);
    jit.registerFunction("flux_qt_set_fixed_size", (void*)&flux_qt_set_fixed_size);
    jit.registerFunction("flux_qt_get_value", (void*)&flux_qt_get_value);
    jit.registerFunction("flux_qt_set_value", (void*)&flux_qt_set_value);
    jit.registerFunction("flux_qt_set_range", (void*)&flux_qt_set_range);
    jit.registerFunction("flux_qt_set_stylesheet", (void*)&flux_qt_set_stylesheet);
    jit.registerFunction("flux_qt_connect", (void*)&flux_qt_connect);
    jit.registerFunction("flux_qt_add_widget", (void*)&flux_qt_add_widget);
    jit.registerFunction("flux_sim_get_vector_size", (void*)&flux_sim_get_vector_size);
    jit.registerFunction("flux_sim_get_vector_val", (void*)&flux_sim_get_vector_val);
    jit.registerFunction("flux_sim_get_vector_x", (void*)&flux_sim_get_vector_x);
    jit.registerFunction("flux_run_sim", (void*)&flux_run_sim);
    jit.registerFunction("flux_get_project_name", (void*)&flux_get_project_name);
    jit.registerFunction("flux_plot_point", (void*)&flux_plot_point);
    jit.registerFunction("flux_to_str", (void*)&flux_to_str);
    jit.registerFunction("flux_concat", (void*)&flux_concat);

    // SPICE runtime functions must be registered for extensions that use simulation API
    jit.registerFunction("flux_set_parameter", (void*)&flux_set_parameter);
    jit.registerFunction("flux_get_parameter", (void*)&flux_get_parameter);
    jit.registerFunction("flux_get_voltage", (void*)&flux_get_voltage);
    jit.registerFunction("flux_get_current", (void*)&flux_get_current);
    jit.registerFunction("flux_register_analysis", (void*)&flux_register_analysis);
    jit.registerFunction("flux_register_measure", (void*)&flux_register_measure);
    jit.registerFunction("flux_register_probe", (void*)&flux_register_probe);
    jit.registerFunction("flux_register_save", (void*)&flux_register_save);

    // Extension config persistence
    jit.registerFunction("flux_config_get", (void*)&flux_config_get);
    jit.registerFunction("flux_config_set", (void*)&flux_config_set);
    jit.registerFunction("flux_config_get_str", (void*)&flux_config_get_str);
    jit.registerFunction("flux_config_set_str", (void*)&flux_config_set_str);

    // Extension sandbox
    jit.registerFunction("flux_has_permission", (void*)&flux_has_permission);
    jit.registerFunction("flux_extension_id", (void*)&flux_extension_id);

    // Inter-extension events
    jit.registerFunction("flux_on_event", (void*)&flux_on_event);
    jit.registerFunction("flux_on_event_all", (void*)&flux_on_event_all);
    jit.registerFunction("flux_emit_event", (void*)&flux_emit_event);
    jit.registerFunction("flux_emit_event_data", (void*)&flux_emit_event_data);

    fprintf(stderr, "[Bridge] Registered all functions including sandbox and events\n");
}
