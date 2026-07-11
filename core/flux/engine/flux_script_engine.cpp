/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flux_script_engine.h"
#include "../bridges/flux_qt_bridge.h"
#include <flux/jit_engine.h>
#include <flux/flux_eigen.h>
#include <QDebug>
#include <string>

// Forward declarations from flux_qt_bridge.cpp
void register_flux_qt_jit_symbols();

FluxScriptEngine& FluxScriptEngine::instance() {
    static FluxScriptEngine inst;
    return inst;
}

FluxScriptEngine::FluxScriptEngine(QObject* parent) : QObject(parent) {
}

extern "C" double flux_qt_adopt(double);
extern "C" double flux_qt_list_widgets(double);
extern "C" void flux_qt_embed(double, double);
extern "C" double flux_qt_get_widget_info(double, double);
extern "C" double flux_qt_create_scope();
extern "C" double flux_qt_create_waveform_viewer();

void FluxScriptEngine::initialize() { 
    Flux::JITEngine::instance().initialize();
    register_flux_qt_jit_symbols();

    // Ensure adoption functions are registered on the same JIT instance
    auto& jit = Flux::JITEngine::instance();
    jit.registerFunction("flux_qt_adopt", (void*)&flux_qt_adopt);
    jit.registerFunction("flux_qt_list_widgets", (void*)&flux_qt_list_widgets);
    jit.registerFunction("flux_qt_embed", (void*)&flux_qt_embed);
    jit.registerFunction("flux_qt_get_widget_info", (void*)&flux_qt_get_widget_info);
    jit.registerFunction("flux_qt_create_scope", (void*)&flux_qt_create_scope);
    jit.registerFunction("flux_qt_create_waveform_viewer", (void*)&flux_qt_create_waveform_viewer);
}

void FluxScriptEngine::finalize() { 
    Flux::JITEngine::instance().finalize(); 
}

bool FluxScriptEngine::isInitialized() const { 
    return Flux::JITEngine::instance().isInitialized(); 
}

bool FluxScriptEngine::executeString(const QString& code, QString* error) {
    std::string stdError;
    bool ok = Flux::JITEngine::instance().executeString(code.toStdString(), error ? &stdError : nullptr);
    if (error) *error = QString::fromStdString(stdError);
    return ok;
}

bool FluxScriptEngine::validateScript(const QString& code, QString* error) {
    std::string stdError;
    bool ok = Flux::JITEngine::instance().compileScript(code.toStdString(), error ? &stdError : nullptr);
    if (error) *error = QString::fromStdString(stdError);
    return ok;
}

FluxScriptEngine::FluxValue FluxScriptEngine::callFunction(const char* method, const std::vector<double>& args, QString* error) {
    std::string stdError;
    auto result = Flux::JITEngine::instance().callFunction(std::string(method), args, error ? &stdError : nullptr);

    if (error) {
        *error = QString::fromStdString(stdError);
        if (!error->isEmpty()) {
            qDebug() << "[FluxScriptEngine] Error calling function:" << method << ":" << *error;
            return 0.0;
        }
    }

    if (std::holds_alternative<double>(result)) {
        return std::get<double>(result);
    } else if (std::holds_alternative<int>(result)) {
        return std::get<int>(result);
    } else if (std::holds_alternative<std::complex<double>>(result)) {
        return std::get<std::complex<double>>(result);
    } else if (std::holds_alternative<Flux::MatrixResult>(result)) {
        const auto& mat = std::get<Flux::MatrixResult>(result);
        return FluxMatrixHandle{mat.ptr, mat.rows, mat.cols};
    }
    
    return 0.0;
}
