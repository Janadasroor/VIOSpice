/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ide_completer.h"
#include <QStandardItemModel>
#include <QIcon>

namespace IDE {

IdeCompleter::IdeCompleter(QObject* parent)
    : Flux::FluxCompleter(parent) {
}

void IdeCompleter::updateCompletions() {
    Flux::FluxCompleter::updateCompletions();
    if (!m_apiAdded) {
        addApiFunctions();
        addMathBuiltins();
        addFluxScriptKeywords();
        m_apiAdded = true;
    }
}

void IdeCompleter::addCategory(const QString& name) {
    auto* model = qobject_cast<QStandardItemModel*>(this->model());
    if (!model) return;

    QStandardItem* sep = new QStandardItem("--- " + name + " ---");
    sep->setEditable(false);
    sep->setEnabled(false);
    QFont f = sep->font();
    f.setBold(true);
    sep->setFont(f);
    sep->setForeground(QColor("#569cd6"));
    model->appendRow(sep);
}

void IdeCompleter::addApiFunctions() {
    addCategory("Qt Widget API");

    struct ApiFunc {
        const char* name;
        const char* desc;
    };

    static const ApiFunc qtWidgetFuncs[] = {
        {"flux_qt_create_window", "Create a dialog window"},
        {"flux_qt_create_button", "Create a push button"},
        {"flux_qt_create_label", "Create a text label"},
        {"flux_qt_create_slider", "Create a slider (0=h, 1=v)"},
        {"flux_qt_create_lcd", "Create an LCD display"},
        {"flux_qt_create_combobox", "Create a combo box"},
        {"flux_qt_create_lineedit", "Create a line edit"},
        {"flux_qt_create_checkbox", "Create a checkbox"},
        {"flux_qt_create_spinbox", "Create a spin box"},
        {"flux_qt_create_progressbar", "Create a progress bar"},
        {"flux_qt_create_tableview", "Create a table view"},
        {"flux_qt_create_tabwidget", "Create a tab widget"},
        {"flux_qt_create_timer", "Create a timer"},
        {"flux_qt_create_layout", "Create a layout (vbox/hbox/grid)"},
        {"flux_qt_add_widget", "Add widget to container"},
        {"flux_qt_set_layout", "Set layout on widget"},
        {"flux_qt_layout_add_widget", "Add widget to layout"},
        {"flux_qt_set_window_size", "Set window size"},
        {"flux_qt_set_text", "Set text on widget"},
        {"flux_qt_get_text", "Get text from widget"},
        {"flux_qt_store", "Store widget handle by name"},
        {"flux_qt_load", "Load widget handle by name"},
        {"flux_qt_on_click_by_name", "Connect button clicked signal"},
        {"flux_qt_on_value_changed_by_name", "Connect value changed signal"},
        {"flux_qt_on_toggled_by_name", "Connect toggled signal"},
        {"flux_qt_msg_box", "Show message box"},
        {nullptr, nullptr}
    };

    for (int i = 0; qtWidgetFuncs[i].name; ++i) {
        QStandardItem* item = new QStandardItem(
            QIcon(":/icons/comp_ic.svg"),
            QString("%1").arg(qtWidgetFuncs[i].name)
        );
        item->setData(QString("Qt Widget: %1").arg(qtWidgetFuncs[i].desc), Qt::UserRole);
        auto* model = qobject_cast<QStandardItemModel*>(this->model());
        if (model) model->appendRow(item);
    }

    addCategory("Workspace API");

    static const ApiFunc workspaceFuncs[] = {
        {"viora_flux_print", "Print message to console"},
        {"flux_get_var", "Get global variable"},
        {"flux_set_var", "Set global variable"},
        {"flux_run_sim", "Run simulation (tran/ac/dc/op)"},
        {"flux_sim_get_vector_size", "Get simulation vector size"},
        {"flux_sim_get_vector_val", "Get simulation Y value"},
        {"flux_sim_get_vector_x", "Get simulation X value"},
        {"flux_set_prop", "Set component property (numeric)"},
        {"flux_set_prop_str", "Set component property (string)"},
        {"flux_get_project_name", "Get current project name"},
        {"flux_get_schematic_file", "Get current schematic file"},
        {"flux_plot_point", "Add point to waveform viewer"},
        {"flux_to_str", "Convert number to string handle"},
        {"flux_concat", "Concatenate two strings"},
        {nullptr, nullptr}
    };

    for (int i = 0; workspaceFuncs[i].name; ++i) {
        QStandardItem* item = new QStandardItem(
            QIcon(":/icons/tool_run.svg"),
            QString("%1").arg(workspaceFuncs[i].name)
        );
        item->setData(QString("Workspace: %1").arg(workspaceFuncs[i].desc), Qt::UserRole);
        auto* model = qobject_cast<QStandardItemModel*>(this->model());
        if (model) model->appendRow(item);
    }

    addCategory("Design Rule API");

    static const ApiFunc ercFuncs[] = {
        {"flux_erc_get_component_count", "Get number of components"},
        {"flux_erc_get_ref", "Get component reference name"},
        {"flux_erc_get_value", "Get component value"},
        {"flux_erc_get_type", "Get component type"},
        {"flux_erc_get_pin_count", "Get pin count on component"},
        {"flux_erc_get_pin_net", "Get net name at pin"},
        {"flux_erc_report", "Report an ERC violation"},
        {nullptr, nullptr}
    };

    for (int i = 0; ercFuncs[i].name; ++i) {
        QStandardItem* item = new QStandardItem(
            QIcon(":/icons/tool_search.svg"),
            QString("%1").arg(ercFuncs[i].name)
        );
        item->setData(QString("ERC: %1").arg(ercFuncs[i].desc), Qt::UserRole);
        auto* model = qobject_cast<QStandardItemModel*>(this->model());
        if (model) model->appendRow(item);
    }
}

void IdeCompleter::addMathBuiltins() {
    addCategory("Math Built-ins");

    static const char* mathFuncs[] = {
        "sin", "cos", "tan", "asin", "acos", "atan",
        "sqrt", "pow", "exp", "log", "log10",
        "abs", "floor", "ceil", "min", "max", "pi",
        nullptr
    };

    auto* model = qobject_cast<QStandardItemModel*>(this->model());
    if (!model) return;

    for (int i = 0; mathFuncs[i]; ++i) {
        QStandardItem* item = new QStandardItem(
            QIcon(":/icons/comp_logic.svg"),
            QString(mathFuncs[i])
        );
        item->setData("Math", Qt::UserRole);
        model->appendRow(item);
    }
}

void IdeCompleter::addFluxScriptKeywords() {
    addCategory("FluxScript Keywords");

    static const char* keywords[] = {
        "def", "extern", "return", "var", "let", "fn",
        "if", "else", "for", "in", "do", "while",
        "import", "switch", "case", "default",
        "break", "continue", "struct", "class", "namespace",
        "adevice", "net", "component", "range",
        nullptr
    };

    auto* model = qobject_cast<QStandardItemModel*>(this->model());
    if (!model) return;

    for (int i = 0; keywords[i]; ++i) {
        QStandardItem* item = new QStandardItem(
            QIcon(":/icons/comp_logic.svg"),
            QString(keywords[i])
        );
        item->setData("Keyword", Qt::UserRole);
        model->appendRow(item);
    }
}

} // namespace IDE
