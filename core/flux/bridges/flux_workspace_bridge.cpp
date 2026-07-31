/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "flux_workspace_bridge.h"
#include "../simulation/jit_context_manager.h"
#include "flux_design_rule_bridge.h"
#include "../schematic/editor/schematic_editor.h"
#include "../schematic/editor/schematic_api.h"
#include "../simulator/bridge/sim_manager.h"
#include "../../simulator/core/sim_results.h"
#include "../ui/waveform_viewer.h"
#include "../extensions/extension_manager.h"
#include "../extensions/extension_sandbox.h"
#include "../extensions/extension_events.h"
#include <flux/jit_engine.h>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QEventLoop>
#include <iostream>
#include <deque>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

static const char* dbl_to_str(double d) {
    uint64_t raw;
    std::memcpy(&raw, &d, sizeof(raw));
    return reinterpret_cast<const char*>(static_cast<uintptr_t>(raw));
}

static QMap<QString, double> g_fluxVars;
static SchematicAPI* g_activeApi = nullptr;
static std::deque<std::string> g_stringPool;

namespace Flux {
namespace Core {

void FluxWorkspaceBridge::setVariable(const QString& name, double value) {
    g_fluxVars[name] = value;
}

double FluxWorkspaceBridge::getVariable(const QString& name) {
    return g_fluxVars.value(name, 0.0);
}

void FluxWorkspaceBridge::setComponentProperty(const char* ref, const char* prop, double value) {
    if (!g_activeApi) return;
    g_activeApi->setProperty(QString::fromUtf8(ref), QString::fromUtf8(prop), value);
}

void FluxWorkspaceBridge::setComponentPropertyStr(const char* ref, const char* prop, const char* value) {
    if (!g_activeApi) return;
    g_activeApi->setProperty(QString::fromUtf8(ref), QString::fromUtf8(prop), QString::fromUtf8(value));
}

// Internal helper for VioSpice to hook the API
void set_active_schematic_api(SchematicAPI* api) {
    g_activeApi = api;
}

const char* pool_workspace_string(const QString& s) {
    static std::mutex poolMutex;
    std::lock_guard<std::mutex> lock(poolMutex);
    g_stringPool.push_back(s.toStdString());
    return g_stringPool.back().c_str();
}

} // namespace Core
} // namespace Flux

extern "C" {
    double flux_get_var(double name_dbl) {
        const char* name = dbl_to_str(name_dbl);
        if (!name) return 0.0;
        return Flux::Core::FluxWorkspaceBridge::getVariable(QString::fromUtf8(name));
    }

    void flux_set_var(double name_dbl, double value) {
        const char* name = dbl_to_str(name_dbl);
        if (name) Flux::Core::FluxWorkspaceBridge::setVariable(QString::fromUtf8(name), value);
    }
    
    void flux_set_prop(double ref_dbl, double prop_dbl, double value) {
        if (!IDE::sandbox().checkPermission(IDE::Permission::SchematicWrite, "set_property"))
            return;
        const char* ref = dbl_to_str(ref_dbl);
        const char* prop = dbl_to_str(prop_dbl);
        Flux::Core::FluxWorkspaceBridge::setComponentProperty(ref, prop, value);
    }

    void flux_set_prop_str(double ref_dbl, double prop_dbl, double value_dbl) {
        if (!IDE::sandbox().checkPermission(IDE::Permission::SchematicWrite, "set_property_str"))
            return;
        const char* ref = dbl_to_str(ref_dbl);
        const char* prop = dbl_to_str(prop_dbl);
        const char* value = dbl_to_str(value_dbl);
        Flux::Core::FluxWorkspaceBridge::setComponentPropertyStr(ref, prop, value);
    }

    // --- Simulation Data Hooks ---
    
    int flux_sim_get_vector_size(double name_dbl) {
        const char* name = dbl_to_str(name_dbl);
        auto* res = Flux::JITContextManager::instance().getSimulationResults();
        if (!res || !name) return 0;
        std::string n(name);
        for (const auto& w : res->waveforms) {
            if (w.name == n) return static_cast<int>(w.yData.size());
        }
        return 0;
    }
    
    double flux_sim_get_vector_val(double name_dbl, int index) {
        const char* name = dbl_to_str(name_dbl);
        auto* res = Flux::JITContextManager::instance().getSimulationResults();
        if (!res || !name) return 0.0;
        std::string n(name);
        for (const auto& w : res->waveforms) {
            if (w.name == n && index >= 0 && index < static_cast<int>(w.yData.size())) {
                return w.yData[index];
            }
        }
        return 0.0;
    }
    
    double flux_sim_get_vector_x(double name_dbl, int index) {
        const char* name = dbl_to_str(name_dbl);
        auto* res = Flux::JITContextManager::instance().getSimulationResults();
        if (!res || !name) return 0.0;
        std::string n(name);
        for (const auto& w : res->waveforms) {
            if (w.name == n && index >= 0 && index < static_cast<int>(w.xData.size())) {
                return w.xData[index];
            }
        }
        return 0.0;
    }

    void flux_run_sim(double analysis_dbl, double tStop, double tStep) {
        if (!IDE::sandbox().checkPermission(IDE::Permission::SimulationRun, "run_simulation"))
            return;
        const char* analysisType = dbl_to_str(analysis_dbl);
        auto* editor = qobject_cast<SchematicEditor*>(QApplication::activeWindow());
        if (!editor) return;

        QString type = QString::fromUtf8(analysisType).toLower();
        
        // Live/interactive mode: start real-time simulation, return immediately (don't block)
        if (type == "live" || type == "interactive") {
            SimulationSetupDialog::Config cfg;
            cfg.type = SimAnalysisType::RealTime;
            cfg.rtStep = (tStep > 0.0) ? tStep : 1e-3;
            cfg.rtMaxTime = (tStop > 0.0) ? tStop : 0.0;
            cfg.stop = cfg.rtMaxTime;
            editor->runSimulationConfig(cfg);
            return;
        }

        SimulationSetupDialog::Config cfg;
        if (type == "tran" || type == "transient") cfg.type = SimAnalysisType::Transient;
        else if (type == "ac") cfg.type = SimAnalysisType::AC;
        else if (type == "op") cfg.type = SimAnalysisType::OP;
        else cfg.type = SimAnalysisType::Transient;

        cfg.stop = tStop;
        cfg.step = tStep;

        // Run the simulation
        QEventLoop loop;
        auto* sim = &SimManager::instance();
        
        QObject::connect(sim, &SimManager::simulationFinished, &loop, &QEventLoop::quit);
        QObject::connect(sim, &SimManager::errorOccurred, &loop, &QEventLoop::quit);

        editor->runSimulationConfig(cfg);

        // Block until finished
        loop.exec();
    }

    double flux_get_project_name() {
        const char* result = g_activeApi ? Flux::Core::pool_workspace_string(g_activeApi->projectName()) : "Untitled Project";
        uint64_t raw = reinterpret_cast<uintptr_t>(result);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }

    double flux_get_schematic_file() {
        const char* result = g_activeApi ? Flux::Core::pool_workspace_string(g_activeApi->filePath()) : "untitled.viosch";
        uint64_t raw = reinterpret_cast<uintptr_t>(result);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }

    double flux_get_open_schematics() {
        auto* editor = qobject_cast<SchematicEditor*>(QApplication::activeWindow());
        if (!editor) { double d = 0; uint64_t raw = 0; std::memcpy(&d, &raw, sizeof(d)); return d; }
        
        auto* tabs = editor->findChild<QTabWidget*>();
        if (!tabs) { double d = 0; uint64_t raw = 0; std::memcpy(&d, &raw, sizeof(d)); return d; }
        
        QStringList names;
        for (int i = 0; i < tabs->count(); ++i) {
            names << tabs->tabText(i).remove("*"); // Clean modified markers
        }
        const char* result = Flux::Core::pool_workspace_string(names.join(","));
        uint64_t raw = reinterpret_cast<uintptr_t>(result);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }

    void flux_select_schematic(double fileName_dbl) {
        const char* fileName = dbl_to_str(fileName_dbl);
        if (!fileName) return;
        auto* editor = qobject_cast<SchematicEditor*>(QApplication::activeWindow());
        if (!editor) return;
        
        auto* tabs = editor->findChild<QTabWidget*>();
        if (!tabs) return;

        QString target = QString::fromUtf8(fileName).toLower();
        for (int i = 0; i < tabs->count(); ++i) {
            QString current = tabs->tabText(i).toLower().remove("*");
            if (current == target) {
                tabs->setCurrentIndex(i);
                // The onTabChanged handler in editor will sync the API
                break;
            }
        }
    }
    
    // --- Standard Output Hook ---
    
    void viora_flux_print(double msg_dbl) {
        const char* msg = dbl_to_str(msg_dbl);
        if (!msg) return;
        printf("[STDOUT] %s\n", msg);
        fflush(stdout);
        Flux::JITContextManager::instance().logMessage(QString::fromUtf8(msg));
    }

    void flux_print_num(double val) {
        QString s = QString::number(val);
        printf("[STDOUT] %s\n", s.toUtf8().constData());
        fflush(stdout);
        Flux::JITContextManager::instance().logMessage(s);
    }

    double flux_to_str(double val) {
        QString s = QString::number(val);
        const char* result = Flux::Core::pool_workspace_string(s);
        uint64_t raw = reinterpret_cast<uintptr_t>(result);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }

    double flux_concat(double s1_dbl, double s2_dbl) {
        const char* s1 = dbl_to_str(s1_dbl);
        const char* s2 = dbl_to_str(s2_dbl);
        if (!s1) return s2_dbl;
        if (!s2) return s1_dbl;
        QString res = QString::fromUtf8(s1) + QString::fromUtf8(s2);
        const char* result = Flux::Core::pool_workspace_string(res);
        uint64_t raw = reinterpret_cast<uintptr_t>(result);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }

    // --- Extension Config Persistence ---

    // Get the active extension directory (from env var or ExtensionManager)
    static QString getActiveExtensionDir() {
        // Check environment variable first (set by CLI ext-run)
        QByteArray envDir = qgetenv("VIORA_EXTENSION_DIR");
        if (!envDir.isEmpty()) {
            return QString::fromUtf8(envDir);
        }

        // Fall back to ExtensionManager
        auto& mgr = ::ExtensionManager::instance();
        auto extensions = mgr.listExtensions();
        for (const auto& ext : extensions) {
            if (ext.loaded) {
                return mgr.listExtensions().isEmpty() ? QString() :
                    // Find the extension's directory
                    QDir(QDir::homePath() + "/.config/VioraEDA/extensions/" + ext.id).absolutePath();
            }
        }
        return QString();
    }

    // Read a config value: flux_config_get("key", defaultValue)
    double flux_config_get(double key_dbl, double defaultVal) {
        if (!IDE::sandbox().checkPermission(IDE::Permission::ConfigRead, "config_get"))
            return defaultVal;
        const char* key = dbl_to_str(key_dbl);
        if (!key) return defaultVal;

        QString extDir = getActiveExtensionDir();
        if (extDir.isEmpty()) return defaultVal;

        QFile cf(extDir + "/config.json");
        if (!cf.exists()) return defaultVal;
        if (!cf.open(QIODevice::ReadOnly)) return defaultVal;

        QJsonDocument doc = QJsonDocument::fromJson(cf.readAll());
        cf.close();

        QJsonObject settings = doc.object();
        QJsonValue val = settings.value(QString::fromUtf8(key));
        if (val.isUndefined()) return defaultVal;
        return val.toDouble();
    }

    // Store a config value: flux_config_set("key", value)
    void flux_config_set(double key_dbl, double value) {
        if (!IDE::sandbox().checkPermission(IDE::Permission::ConfigWrite, "config_set"))
            return;
        const char* key = dbl_to_str(key_dbl);
        if (!key) return;

        QString extDir = getActiveExtensionDir();
        if (extDir.isEmpty()) return;

        QString configPath = extDir + "/config.json";
        QJsonObject settings;

        QFile readFile(configPath);
        if (readFile.open(QIODevice::ReadOnly)) {
            settings = QJsonDocument::fromJson(readFile.readAll()).object();
            readFile.close();
        }

        settings[QString::fromUtf8(key)] = value;

        QFile writeFile(configPath);
        if (writeFile.open(QIODevice::WriteOnly)) {
            writeFile.write(QJsonDocument(settings).toJson(QJsonDocument::Indented));
            writeFile.close();
        }
    }

    // Read a string config value: flux_config_get_str("key", defaultStr)
    double flux_config_get_str(double key_dbl, double defaultStr) {
        const char* key = dbl_to_str(key_dbl);
        if (!key) return defaultStr;

        QString extDir = getActiveExtensionDir();
        if (extDir.isEmpty()) return defaultStr;

        QFile cf(extDir + "/config.json");
        if (!cf.exists()) return defaultStr;
        if (!cf.open(QIODevice::ReadOnly)) return defaultStr;

        QJsonDocument doc = QJsonDocument::fromJson(cf.readAll());
        cf.close();

        QJsonObject settings = doc.object();
        QJsonValue val = settings.value(QString::fromUtf8(key));
        if (val.isUndefined()) return defaultStr;

        QString result = val.toString();
        const char* pooled = Flux::Core::pool_workspace_string(result);
        uint64_t raw = reinterpret_cast<uintptr_t>(pooled);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }

    // Store a string config value: flux_config_set_str("key", "value")
    void flux_config_set_str(double key_dbl, double value_dbl) {
        const char* key = dbl_to_str(key_dbl);
        const char* value = dbl_to_str(value_dbl);
        if (!key || !value) return;

        QString extDir = getActiveExtensionDir();
        if (extDir.isEmpty()) return;

        QString configPath = extDir + "/config.json";
        QJsonObject settings;

        QFile readFile(configPath);
        if (readFile.open(QIODevice::ReadOnly)) {
            settings = QJsonDocument::fromJson(readFile.readAll()).object();
            readFile.close();
        }

        settings[QString::fromUtf8(key)] = QString::fromUtf8(value);

        QFile writeFile(configPath);
        if (writeFile.open(QIODevice::WriteOnly)) {
            writeFile.write(QJsonDocument(settings).toJson(QJsonDocument::Indented));
            writeFile.close();
        }
    }

    // --- Inter-Extension Events ---

    // Subscribe to an event: flux_on_event("event_name")
    // The callback function name is derived from the event name
    void flux_on_event(double event_name_dbl) {
        const char* eventName = dbl_to_str(event_name_dbl);
        if (!eventName) return;

        QString extId = IDE::sandbox().currentExtensionId();
        if (extId.isEmpty()) return;

        QString eventStr = QString::fromUtf8(eventName);

        // Build callback function name from event name
        // e.g., "simulation.started" -> "on_simulation_started"
        QString callbackName = "on_" + eventStr;
        callbackName.replace(".", "_");

        IDE::eventBus().subscribe(extId, eventStr,
            [extId, callbackName](const IDE::ExtensionEvent& event) {
                Q_UNUSED(event);
                // Call the callback function in the FluxScript engine
                std::string error;
                Flux::JITEngine::instance().callFunction(
                    callbackName.toUtf8().constData(), {}, &error);
                if (!error.empty()) {
                    qDebug() << "[EventBus] Callback error:" << QString::fromStdString(error);
                }
            });
    }

    // Subscribe to all events: flux_on_event_all()
    void flux_on_event_all() {
        QString extId = IDE::sandbox().currentExtensionId();
        if (extId.isEmpty()) return;

        IDE::eventBus().subscribe(extId, "*",
            [extId](const IDE::ExtensionEvent& event) {
                qDebug() << "[EventBus]" << extId << "received event:" << event.name;
            });
    }

    // Emit an event: flux_emit_event("event_name")
    void flux_emit_event(double event_name_dbl) {
        const char* eventName = dbl_to_str(event_name_dbl);
        if (!eventName) return;

        QString extId = IDE::sandbox().currentExtensionId();
        QString eventStr = QString::fromUtf8(eventName);

        IDE::eventBus().emitEvent(extId, eventStr);
    }

    // Emit event with data: flux_emit_event_data("event_name", data)
    void flux_emit_event_data(double event_name_dbl, double data_dbl) {
        const char* eventName = dbl_to_str(event_name_dbl);
        if (!eventName) return;

        QString extId = IDE::sandbox().currentExtensionId();
        QString eventStr = QString::fromUtf8(eventName);

        IDE::eventBus().emitEvent(extId, eventStr, data_dbl);
    }

    // --- Extension State Persistence ---

    // Save a state value: flux_state_save("key", value)
    void flux_state_save(double key_dbl, double value) {
        const char* key = dbl_to_str(key_dbl);
        if (!key) return;

        QString extDir = getActiveExtensionDir();
        if (extDir.isEmpty()) return;

        QString statePath = extDir + "/state.json";
        QJsonObject state;
        QFile readFile(statePath);
        if (readFile.open(QIODevice::ReadOnly)) {
            state = QJsonDocument::fromJson(readFile.readAll()).object();
            readFile.close();
        }

        state[QString::fromUtf8(key)] = static_cast<double>(value);

        QFile writeFile(statePath);
        if (writeFile.open(QIODevice::WriteOnly)) {
            writeFile.write(QJsonDocument(state).toJson(QJsonDocument::Indented));
            writeFile.close();
        }
    }

    // Load a state value: flux_state_load("key", defaultValue)
    double flux_state_load(double key_dbl, double defaultVal) {
        const char* key = dbl_to_str(key_dbl);
        if (!key) return defaultVal;

        QString extDir = getActiveExtensionDir();
        if (extDir.isEmpty()) return defaultVal;

        QString statePath = extDir + "/state.json";
        QFile cf(statePath);
        if (!cf.exists()) return defaultVal;
        if (!cf.open(QIODevice::ReadOnly)) return defaultVal;

        QJsonDocument doc = QJsonDocument::fromJson(cf.readAll());
        cf.close();

        QJsonObject state = doc.object();
        QJsonValue val = state.value(QString::fromUtf8(key));
        if (val.isUndefined()) return defaultVal;
        return val.toDouble();
    }

    // Save a string state value
    void flux_state_save_str(double key_dbl, double value_dbl) {
        const char* key = dbl_to_str(key_dbl);
        const char* value = dbl_to_str(value_dbl);
        if (!key || !value) return;

        QString extDir = getActiveExtensionDir();
        if (extDir.isEmpty()) return;

        QString statePath = extDir + "/state.json";
        QJsonObject state;
        QFile readFile(statePath);
        if (readFile.open(QIODevice::ReadOnly)) {
            state = QJsonDocument::fromJson(readFile.readAll()).object();
            readFile.close();
        }

        state[QString::fromUtf8(key)] = QString::fromUtf8(value);

        QFile writeFile(statePath);
        if (writeFile.open(QIODevice::WriteOnly)) {
            writeFile.write(QJsonDocument(state).toJson(QJsonDocument::Indented));
            writeFile.close();
        }
    }

    // Load a string state value
    double flux_state_load_str(double key_dbl, double defaultStr) {
        const char* key = dbl_to_str(key_dbl);
        if (!key) return defaultStr;

        QString extDir = getActiveExtensionDir();
        if (extDir.isEmpty()) return defaultStr;

        QString statePath = extDir + "/state.json";
        QFile cf(statePath);
        if (!cf.exists()) return defaultStr;
        if (!cf.open(QIODevice::ReadOnly)) return defaultStr;

        QJsonDocument doc = QJsonDocument::fromJson(cf.readAll());
        cf.close();

        QJsonObject state = doc.object();
        QJsonValue val = state.value(QString::fromUtf8(key));
        if (val.isUndefined()) return defaultStr;

        QString result = val.toString();
        const char* pooled = Flux::Core::pool_workspace_string(result);
        uint64_t raw = reinterpret_cast<uintptr_t>(pooled);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }

    // Clear all state for current extension
    void flux_state_clear() {
        QString extDir = getActiveExtensionDir();
        if (extDir.isEmpty()) return;

        QString statePath = extDir + "/state.json";
        QFile::remove(statePath);
    }

    // --- Permission Query ---

    // Check if current extension has a permission: flux_has_permission("schematic.write")
    double flux_has_permission(double perm_dbl) {
        const char* perm = dbl_to_str(perm_dbl);
        if (!perm) return 0.0;
        return IDE::sandbox().hasPermission(QString::fromUtf8(perm)) ? 1.0 : 0.0;
    }

    // Get current extension ID: flux_extension_id()
    double flux_extension_id() {
        QString id = IDE::sandbox().currentExtensionId();
        if (id.isEmpty()) {
            double d = 0;
            uint64_t raw = 0;
            std::memcpy(&d, &raw, sizeof(d));
            return d;
        }
        const char* result = Flux::Core::pool_workspace_string(id);
        uint64_t raw = reinterpret_cast<uintptr_t>(result);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }

    // --- Plotting ---

    void flux_plot_point(double series_dbl, double x, double y) {
        if (!IDE::sandbox().checkPermission(IDE::Permission::Plotting, "plot_point"))
            return;
        const char* seriesName = dbl_to_str(series_dbl);
        if (!seriesName) return;
        auto* editor = qobject_cast<SchematicEditor*>(QApplication::activeWindow());
        if (!editor) return;

        auto* viewer = editor->findChild<WaveformViewer*>();
        if (viewer) {
            viewer->appendPoint(QString::fromUtf8(seriesName), x, y);
        }
    }

}
