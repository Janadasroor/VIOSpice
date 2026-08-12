/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

// ext_test.cpp â€” Extension test harness with headless mock GUI
// Usage: viora ext-test <extension-dir> [--verbose] [--json]

#include "ext_test.h"
#include "common.h"
#include "../command_registry.h"

#include <iostream>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QTextStream>
#include <QDateTime>
#include <QCryptographicHash>
#include <QElapsedTimer>

#include "../core/flux/engine/flux_script_engine.h"
#include "../core/flux/extensions/extension_manager.h"
#include "../core/flux/extensions/extension_sandbox.h"
#include "../core/flux/extensions/extension_config.h"
#include "../core/flux/extensions/extension_events.h"

// ============================================================================
// Test Results
// ============================================================================

struct TestResult {
    QString name;
    bool passed;
    QString message;
    qint64 durationMs;
};

struct TestReport {
    QString extensionId;
    QString extensionDir;
    QDateTime timestamp;
    QList<TestResult> results;
    int passed = 0;
    int failed = 0;
    int total = 0;
    qint64 totalDurationMs = 0;

    void addResult(const TestResult& r) {
        results.append(r);
        total++;
        if (r.passed) passed++;
        else failed++;
        totalDurationMs += r.durationMs;
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["extensionId"] = extensionId;
        obj["extensionDir"] = extensionDir;
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        obj["passed"] = passed;
        obj["failed"] = failed;
        obj["total"] = total;
        obj["totalDurationMs"] = totalDurationMs;

        QJsonArray arr;
        for (const auto& r : results) {
            QJsonObject robj;
            robj["name"] = r.name;
            robj["passed"] = r.passed;
            robj["message"] = r.message;
            robj["durationMs"] = r.durationMs;
            arr.append(robj);
        }
        obj["results"] = arr;
        return obj;
    }
};

// ============================================================================
// Mock GUI System
// ============================================================================

// Track all GUI operations for verification
struct GuiMock {
    struct WidgetCall {
        QString function;
        QVariantList args;
        double result;
    };

    struct MsgBoxCall {
        QString title;
        QString text;
    };

    QList<WidgetCall> widgetCalls;
    QList<MsgBoxCall> msgBoxCalls;
    int widgetCount = 0;

    void reset() {
        widgetCalls.clear();
        msgBoxCalls.clear();
        widgetCount = 0;
    }

    int callCount(const QString& func) const {
        int count = 0;
        for (const auto& c : widgetCalls) {
            if (c.function == func) count++;
        }
        return count;
    }
};

static GuiMock s_guiMock;

// Mock functions that capture calls instead of creating real widgets
extern "C" double mock_flux_qt_create_window(double title_dbl) {
    GuiMock::WidgetCall call;
    call.function = "create_window";
    call.args.append(title_dbl);
    call.result = static_cast<double>(s_guiMock.widgetCount);
    s_guiMock.widgetCalls.append(call);
    return static_cast<double>(s_guiMock.widgetCount++);
}

extern "C" double mock_flux_qt_create_button(double text_dbl) {
    GuiMock::WidgetCall call;
    call.function = "create_button";
    call.args.append(text_dbl);
    call.result = static_cast<double>(s_guiMock.widgetCount);
    s_guiMock.widgetCalls.append(call);
    return static_cast<double>(s_guiMock.widgetCount++);
}

extern "C" double mock_flux_qt_create_label(double text_dbl) {
    GuiMock::WidgetCall call;
    call.function = "create_label";
    call.args.append(text_dbl);
    call.result = static_cast<double>(s_guiMock.widgetCount);
    s_guiMock.widgetCalls.append(call);
    return static_cast<double>(s_guiMock.widgetCount++);
}

extern "C" double mock_flux_qt_create_spinbox() {
    GuiMock::WidgetCall call;
    call.function = "create_spinbox";
    call.result = static_cast<double>(s_guiMock.widgetCount);
    s_guiMock.widgetCalls.append(call);
    return static_cast<double>(s_guiMock.widgetCount++);
}

extern "C" double mock_flux_qt_create_layout(double type_dbl) {
    GuiMock::WidgetCall call;
    call.function = "create_layout";
    call.args.append(type_dbl);
    call.result = static_cast<double>(s_guiMock.widgetCount);
    s_guiMock.widgetCalls.append(call);
    return static_cast<double>(s_guiMock.widgetCount++);
}

extern "C" void mock_flux_qt_set_layout(double, double) {
    GuiMock::WidgetCall call;
    call.function = "set_layout";
    s_guiMock.widgetCalls.append(call);
}

extern "C" void mock_flux_qt_layout_add_widget(double, double) {
    GuiMock::WidgetCall call;
    call.function = "layout_add_widget";
    s_guiMock.widgetCalls.append(call);
}

extern "C" void mock_flux_qt_set_window_size(double, double, double) {
    GuiMock::WidgetCall call;
    call.function = "set_window_size";
    s_guiMock.widgetCalls.append(call);
}

extern "C" void mock_flux_qt_on_click_by_name(double, double) {
    GuiMock::WidgetCall call;
    call.function = "on_click_by_name";
    s_guiMock.widgetCalls.append(call);
}

extern "C" void mock_flux_qt_msg_box(double title_dbl, double text_dbl) {
    GuiMock::MsgBoxCall mb;
    s_guiMock.msgBoxCalls.append(mb);
    GuiMock::WidgetCall call;
    call.function = "msg_box";
    call.args.append(title_dbl);
    call.args.append(text_dbl);
    s_guiMock.widgetCalls.append(call);
}

extern "C" void mock_flux_qt_set_property(double, double, double) {
    s_guiMock.widgetCalls.append({"set_property", {}, 0});
}

extern "C" double mock_flux_qt_get_property(double, double) {
    s_guiMock.widgetCalls.append({"get_property", {}, 0});
    return 0;
}

extern "C" void mock_flux_qt_set_text(double, double) {
    s_guiMock.widgetCalls.append({"set_text", {}, 0});
}

// ============================================================================
// Test Runner
// ============================================================================

class ExtensionTestRunner {
public:
    ExtensionTestRunner(const QString& extDir, bool verbose)
        : m_extDir(extDir), m_verbose(verbose) {}

    TestReport run() {
        TestReport report;
        report.extensionDir = m_extDir;
        report.timestamp = QDateTime::currentDateTime();

        // Read manifest
        QFile mf(m_extDir + "/manifest.json");
        if (!mf.open(QIODevice::ReadOnly)) {
            report.addResult({"manifest_exists", false, "manifest.json not found"});
            return report;
        }

        QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
        report.extensionId = manifest["id"].toString();

        // Test 1: Manifest validation
        auto t1 = timeIt([&]() { return validateManifest(manifest); });
        report.addResult(t1);

        // Test 2: Main file compilation
        auto t2 = timeIt([&]() { return compileMainFile(manifest); });
        report.addResult(t2);

        // Test 3: Config initialization
        auto t3 = timeIt([&]() { return testConfigInit(manifest); });
        report.addResult(t3);

        // Test 4: Permission declarations
        auto t4 = timeIt([&]() { return testPermissions(manifest); });
        report.addResult(t4);

        // Test 5: Dependency declarations
        auto t5 = timeIt([&]() { return testDependencies(manifest); });
        report.addResult(t5);

        // Test 6: Mock GUI execution
        auto t6 = timeIt([&]() { return testMockExecution(manifest); });
        report.addResult(t6);

        // Test 7: Config persistence
        auto t7 = timeIt([&]() { return testConfigPersistence(manifest); });
        report.addResult(t7);

        return report;
    }

private:
    QString m_extDir;
    bool m_verbose;

    template<typename Func>
    TestResult timeIt(Func func) {
        QElapsedTimer timer;
        timer.start();
        TestResult result = func();
        result.durationMs = timer.elapsed();
        return result;
    }

    TestResult validateManifest(const QJsonObject& manifest) {
        if (manifest["id"].toString().isEmpty()) {
            return {"manifest_valid", false, "Missing 'id' field"};
        }
        if (manifest["version"].toString().isEmpty()) {
            return {"manifest_valid", false, "Missing 'version' field"};
        }
        if (manifest["main"].toString().isEmpty()) {
            return {"manifest_valid", false, "Missing 'main' field"};
        }
        return {"manifest_valid", true, "All required fields present"};
    }

    TestResult compileMainFile(const QJsonObject& manifest) {
        QString mainFile = manifest["main"].toString("main.flux");
        QFile sf(m_extDir + "/" + mainFile);
        if (!sf.exists()) {
            return {"compile_main", false, "Main file not found: " + mainFile};
        }
        if (!sf.open(QIODevice::ReadOnly)) {
            return {"compile_main", false, "Cannot read main file"};
        }

        FluxScriptEngine::instance().initialize();
        QString source = QString::fromUtf8(sf.readAll());
        QString error;
        if (!FluxScriptEngine::instance().executeString(source, &error)) {
            return {"compile_main", false, "Compile error: " + error};
        }
        return {"compile_main", true, "Compiles successfully"};
    }

    TestResult testConfigInit(const QJsonObject& manifest) {
        QString id = manifest["id"].toString();
        QString configPath = m_extDir + "/config.json";

        // Check if config file exists or can be created
        IDE::ExtensionConfig config(m_extDir);
        config.set("test_key", 42.0);
        bool saved = config.save();
        if (!saved) {
            return {"config_init", false, "Cannot save config"};
        }

        // Verify it can be read back
        IDE::ExtensionConfig config2(m_extDir);
        double val = config2.get("test_key", 0.0).toDouble();
        if (val != 42.0) {
            return {"config_init", false, "Config value mismatch"};
        }

        // Clean up test key
        config2.remove("test_key");
        config2.save();

        return {"config_init", true, "Config read/write works"};
    }

    TestResult testPermissions(const QJsonObject& manifest) {
        QJsonArray permArr = manifest["permissions"].toArray();
        QSet<IDE::Permission> perms;
        for (const auto& p : permArr) {
            IDE::Permission perm = IDE::permissionFromString(p.toString().trimmed().toLower());
            if (perm != IDE::Permission::None) {
                perms.insert(perm);
            }
        }

        QString id = manifest["id"].toString();
        IDE::sandbox().setPermissions(id, perms);
        IDE::sandbox().setCurrentExtension(id);

        // Verify permissions are set correctly
        bool allCorrect = true;
        for (const auto& p : permArr) {
            QString permName = p.toString().trimmed().toLower();
            if (!IDE::sandbox().hasPermission(permName)) {
                allCorrect = false;
                break;
            }
        }

        // Verify denied permissions are denied
        if (perms.contains(IDE::Permission::SchematicWrite) == false) {
            if (IDE::sandbox().hasPermission(IDE::Permission::SchematicWrite)) {
                allCorrect = false;
            }
        }

        return {"permissions_valid", allCorrect,
                QString("%1 permission(s) declared").arg(perms.size())};
    }

    TestResult testDependencies(const QJsonObject& manifest) {
        QJsonObject deps = manifest["dependencies"].toObject();
        if (deps.isEmpty()) {
            return {"dependencies_valid", true, "No dependencies declared"};
        }

        // Check if dependencies exist
        QDir extDir(QDir::homePath() + "/.config/VioraEDA/extensions");

        QStringList missing;
        for (auto it = deps.constBegin(); it != deps.constEnd(); ++it) {
            if (!QDir(extDir.filePath(it.key())).exists()) {
                missing.append(it.key());
            }
        }

        if (missing.isEmpty()) {
            return {"dependencies_valid", true,
                    QString("%1 dependency(ies) satisfied").arg(deps.size())};
        }
        return {"dependencies_valid", false,
                "Missing: " + missing.join(", ")};
    }

    TestResult testMockExecution(const QJsonObject& manifest) {
        QString activateHook = manifest["hooks"].toObject()["onActivate"].toString();
        if (activateHook.isEmpty()) {
            return {"mock_execution", true, "No activation hook to test"};
        }

        // Reset mock state
        s_guiMock.reset();
        IDE::eventBus().clearAll();

        // Set sandbox
        QJsonArray permArr = manifest["permissions"].toArray();
        QSet<IDE::Permission> perms;
        for (const auto& p : permArr) {
            IDE::Permission perm = IDE::permissionFromString(p.toString().trimmed().toLower());
            if (perm != IDE::Permission::None) perms.insert(perm);
        }
        IDE::sandbox().setPermissions(manifest["id"].toString(), perms);
        IDE::sandbox().setCurrentExtension(manifest["id"].toString());

        // Set config dir env
        qputenv("VIORA_EXTENSION_DIR", m_extDir.toUtf8());

        // Execute the hook
        FluxScriptEngine::instance().initialize();
        QString hookError;
        auto result = FluxScriptEngine::instance().callFunction(
            activateHook.toUtf8().constData(), {}, &hookError);

        if (!hookError.isEmpty()) {
            return {"mock_execution", false, "Hook failed: " + hookError};
        }

        // Check what GUI calls were made
        int widgetCalls = s_guiMock.callCount("create_window") +
                         s_guiMock.callCount("create_button") +
                         s_guiMock.callCount("create_label") +
                         s_guiMock.callCount("create_spinbox");

        return {"mock_execution", true,
                QString("%1 widget(s) created in mock mode").arg(widgetCalls)};
    }

    TestResult testConfigPersistence(const QJsonObject& manifest) {
        QString id = manifest["id"].toString();

        // Set some config values
        IDE::sandbox().setCurrentExtension(id);
        qputenv("VIORA_EXTENSION_DIR", m_extDir.toUtf8());

        IDE::ExtensionConfig config(m_extDir);
        config.set("persist_test", 999.0);
        config.set("persist_str", "hello");
        config.save();

        // Verify they persist
        IDE::ExtensionConfig config2(m_extDir);
        double val = config2.get("persist_test", 0.0).toDouble();
        QString strVal = config2.get("persist_str").toString();

        bool ok = (val == 999.0 && strVal == "hello");

        // Clean up
        config2.remove("persist_test");
        config2.remove("persist_str");
        config2.save();

        return {"config_persist", ok,
                ok ? "Values persist correctly" : "Persistence failed"};
    }
};

// ============================================================================
// Command Implementation
// ============================================================================

class ExtTestCommand : public CLICommand {
public:
    QString name() const override { return "ext-test"; }
    QString description() const override {
        return "Run extension tests in headless mode with mock GUI.";
    }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("verbose", "Show detailed output"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }

    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora ext-test <extension-dir> [--verbose] [--json]\n";
            return 1;
        }

        QString extDir = args[0];
        bool verbose = parser.isSet("verbose");
        bool jsonOutput = parser.isSet("json");

        if (!QDir(extDir).exists()) {
            std::cerr << "Error: directory not found: " << extDir.toStdString() << "\n";
            return 1;
        }

        if (!QFile::exists(extDir + "/manifest.json")) {
            std::cerr << "Error: manifest.json not found in " << extDir.toStdString() << "\n";
            return 1;
        }

        if (!jsonOutput) {
            std::cout << "Running tests for " << extDir.toStdString() << "...\n\n";
        }

        ExtensionTestRunner runner(extDir, verbose);
        TestReport report = runner.run();

        if (jsonOutput) {
            std::cout << QJsonDocument(report.toJson()).toJson(QJsonDocument::Indented).toStdString()
                      << std::endl;
        } else {
            printReport(report);
        }

        return report.failed == 0 ? 0 : 1;
    }

private:
    void printReport(const TestReport& report) {
        std::cout << "Extension: " << report.extensionId.toStdString() << "\n";
        std::cout << "Directory: " << report.extensionDir.toStdString() << "\n";
        std::cout << "Time: " << report.timestamp.toString().toStdString() << "\n\n";

        for (const auto& r : report.results) {
            QString status = r.passed ? "\033[92mPASS\033[0m" : "\033[91mFAIL\033[0m";
            std::cout << "  " << status.toStdString() << "  " << r.name.toStdString();
            if (!r.message.isEmpty()) {
                std::cout << " â€” " << r.message.toStdString();
            }
            std::cout << " (" << r.durationMs << "ms)\n";
        }

        std::cout << "\n";
        std::cout << "Results: " << report.passed << " passed, "
                  << report.failed << " failed, "
                  << report.total << " total ("
                  << report.totalDurationMs << "ms)\n";

        if (report.failed == 0) {
            std::cout << "\033[92mAll tests passed!\033[0m\n";
        } else {
            std::cout << "\033[91m" << report.failed << " test(s) failed\033[0m\n";
        }
    }
};

// ============================================================================
// Registration
// ============================================================================

void registerExtTestCommand() {
    CommandRegistry::instance().registerCommand(std::make_unique<ExtTestCommand>());
}
