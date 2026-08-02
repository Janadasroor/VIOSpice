/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

// ext_commands.cpp — Comprehensive CLI toolkit for VioraIDE extension development
// Usage:
//   viora ext create <id> [--name NAME] [--author AUTHOR] [--template TYPE]
//   viora ext list [--installed] [--available]
//   viora ext run <extension-dir> [--debug]
//   viora ext validate <extension-dir>
//   viora ext package <extension-dir> [--output PATH]
//   viora ext deps <extension-id>
//   viora ext config <extension-id> [key] [value]
//   viora ext logs <extension-id> [--follow]
//   viora ext new <name> --interactive

#include "ext_commands.h"
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
#include <QProcess>
#include <QStandardPaths>
#include <QDateTime>
#include <QTextStream>
#include <QThread>
#include <QCryptographicHash>

#include "../core/flux/engine/flux_script_engine.h"
#include "../core/flux/extensions/extension_manager.h"
#include "../core/flux/extensions/extension_sandbox.h"
#include <flux/jit_engine.h>

namespace ExtCli {
    static void info(const QString& text) { std::cout << "  " << text.toStdString() << std::endl; }
    static void success(const QString& text) { std::cout << "✓ " << text.toStdString() << std::endl; }
    static void error(const QString& text) { std::cout << "✗ " << text.toStdString() << std::endl; }
    static void header(const QString& text) { std::cout << text.toStdString() << std::endl; }
}

// ============================================================================
// Helpers
// ============================================================================

static QString extensionsDir() {
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/extensions";
#elif defined(Q_OS_MACOS)
    return QDir::homePath() + "/Library/Application Support/VioraEDA/extensions";
#else
    return QDir::homePath() + "/.config/VioraEDA/extensions";
#endif
}

static QString logDir() {
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/extension-logs";
#elif defined(Q_OS_MACOS)
    return QDir::homePath() + "/Library/Application Support/VioraEDA/extension-logs";
#else
    return QDir::homePath() + "/.local/share/VioraEDA/extension-logs";
#endif
}

static void printColor(const QString& text, const QString& color) {
    if (qEnvironmentVariable("NO_COLOR").isEmpty()) {
        std::cout << color.toStdString() << text.toStdString() << "\033[0m" << std::endl;
    } else {
        std::cout << text.toStdString() << std::endl;
    }
}

static void printWarn(const QString& text) { printColor("⚠ " + text, "\033[93m"); }

// ============================================================================
// ext create — Create new extension
// ============================================================================

class ExtCreateCommand : public CLICommand {
public:
    QString name() const override { return "ext-create"; }
    QString description() const override { return "Create a new VioraIDE extension with scaffolding."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("name", "Display name", "name"));
        parser.addOption(QCommandLineOption("author", "Author name", "author"));
        parser.addOption(QCommandLineOption("template", "Template type (empty/panel/calculator/dashboard)", "template", "panel"));
        parser.addOption(QCommandLineOption("desc", "Description", "desc"));
        parser.addOption(QCommandLineOption("version", "Initial version", "version", "0.1.0"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            ExtCli::error("Usage: viora ext-create <id> [--name NAME] [--author AUTHOR] [--template TYPE]");
            return 1;
        }

        QString id = args[0];
        QString name = parser.value("name").isEmpty() ? id : parser.value("name");
        QString author = parser.value("author");
        QString desc = parser.value("desc");
        QString version = parser.value("version");
        QString tmpl = parser.value("template");

        QString extDir = extensionsDir() + "/" + id;
        if (QDir(extDir).exists()) {
            ExtCli::error("Extension '" + id + "' already exists at " + extDir);
            return 1;
        }

        ExtCli::header("Creating extension: " + name);
        ExtCli::info("ID: " + id);
        ExtCli::info("Template: " + tmpl);
        ExtCli::info("Author: " + (author.isEmpty() ? "(none)" : author));

        // Create directory
        QDir().mkpath(extDir);

        // Write manifest.json
        QJsonObject manifest;
        manifest["id"] = id;
        manifest["name"] = name;
        manifest["version"] = version;
        manifest["author"] = author;
        manifest["description"] = desc;
        manifest["main"] = "main.flux";
        QJsonObject hooks;
        hooks["onActivate"] = "init";
        manifest["hooks"] = hooks;
        QJsonArray menuArr;
        QJsonObject menuEntry;
        menuEntry["path"] = name;
        menuEntry["action"] = "open_panel";
        menuArr.append(menuEntry);
        manifest["menu"] = menuArr;

        QFile manifestFile(extDir + "/manifest.json");
        manifestFile.open(QIODevice::WriteOnly);
        manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));

        // Write main.flux
        QString mainFlux = generateTemplate(name, tmpl);
        QFile mainFile(extDir + "/main.flux");
        mainFile.open(QIODevice::WriteOnly);
        mainFile.write(mainFlux.toUtf8());

        // Write config.json with defaults
        QJsonObject config;
        config["theme"] = "auto";
        QFile configFile(extDir + "/config.json");
        configFile.open(QIODevice::WriteOnly);
        configFile.write(QJsonDocument(config).toJson(QJsonDocument::Indented));

        ExtCli::success("Created extension at " + extDir);
        ExtCli::info("Files: manifest.json, main.flux, config.json");
        ExtCli::info("Run: viora ext-run " + extDir);
        return 0;
    }

private:
    QString generateTemplate(const QString& name, const QString& tmpl) {
        if (tmpl == "empty") {
            return QString(R"(// %1 - Empty Extension
def init() {
    viora_flux_print("%1 loaded")
}

def open_panel() {
    // Your UI code here
}

def cleanup() {
    // Called on unload
}
)").arg(name);
        }

        if (tmpl == "dashboard") {
            return QString(R"(// %1 - Dashboard Extension
var chart_handle = 0

def init() {
    viora_flux_print("%1 loaded")
}

def open_panel() {
    win = flux_qt_create_window("%1 Dashboard")
    box = flux_qt_create_layout("vbox")
    flux_qt_set_layout(win, box)

    title = flux_qt_create_label("%1 Dashboard")
    flux_qt_layout_add_widget(box, title)

    btn_run = flux_qt_create_button("Run Simulation")
    flux_qt_layout_add_widget(box, btn_run)
    flux_qt_on_click_by_name(btn_run, "on_run")

    btn_plot = flux_qt_create_button("Plot Results")
    flux_qt_layout_add_widget(box, btn_plot)
    flux_qt_on_click_by_name(btn_plot, "on_plot")

    flux_qt_set_window_size(win, 400.0, 200.0)
}

def on_run() {
    flux_run_sim("tran", 1.0, 0.001)
    viora_flux_print("Simulation started")
}

def on_plot() {
    viora_flux_print("Plotting results...")
}
)").arg(name);
        }

        if (tmpl == "calculator") {
            return QString(R"(// %1 - Calculator Extension
var input1 = 0
var input2 = 0
var result_display = 0

def init() {
    viora_flux_print("%1 loaded")
}

def open_panel() {
    win = flux_qt_create_window("%1")
    box = flux_qt_create_layout("vbox")
    flux_qt_set_layout(win, box)

    lbl1 = flux_qt_create_label("Value 1:")
    flux_qt_layout_add_widget(box, lbl1)
    input1 = flux_qt_create_spinbox()
    flux_qt_set_property(input1, "value", 0.0)
    flux_qt_layout_add_widget(box, input1)

    lbl2 = flux_qt_create_label("Value 2:")
    flux_qt_layout_add_widget(box, lbl2)
    input2 = flux_qt_create_spinbox()
    flux_qt_set_property(input2, "value", 0.0)
    flux_qt_layout_add_widget(box, input2)

    btn = flux_qt_create_button("Calculate")
    flux_qt_layout_add_widget(box, btn)
    flux_qt_on_click_by_name(btn, "on_calculate")

    result_display = flux_qt_create_spinbox()
    flux_qt_set_property(result_display, "readOnly", 1.0)
    flux_qt_layout_add_widget(box, result_display)

    flux_qt_set_window_size(win, 280.0, 250.0)
}

def on_calculate() {
    val1 = flux_qt_get_property(input1, "value")
    val2 = flux_qt_get_property(input2, "value")
    flux_qt_set_property(result_display, "value", val1 + val2)
}
)").arg(name);
        }

        // Default: panel template
        return QString(R"(// %1 - Panel Extension
def init() {
    viora_flux_print("%1 loaded")
}

def open_panel() {
    win = flux_qt_create_window("%1")
    box = flux_qt_create_layout("vbox")
    flux_qt_set_layout(win, box)

    lbl = flux_qt_create_label("Hello from %1!")
    flux_qt_layout_add_widget(box, lbl)

    btn = flux_qt_create_button("Click Me")
    flux_qt_layout_add_widget(box, btn)
    flux_qt_on_click_by_name(btn, "on_click")

    flux_qt_set_window_size(win, 300.0, 150.0)
}

def on_click() {
    flux_qt_msg_box("%1", "Button clicked!")
}
)").arg(name);
    }
};

// ============================================================================
// ext list — List extensions
// ============================================================================

class ExtListCommand : public CLICommand {
public:
    QString name() const override { return "ext-list"; }
    QString description() const override { return "List installed extensions with status."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("json", "Output as JSON"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        QDir dir(extensionsDir());
        if (!dir.exists()) {
            ExtCli::info("No extensions directory found");
            return 0;
        }

        auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (entries.isEmpty()) {
            ExtCli::info("No extensions installed");
            return 0;
        }

        bool jsonOutput = parser.isSet("json");
        if (jsonOutput) {
            QJsonArray arr;
            for (const auto& id : entries) {
                QJsonObject ext;
                ext["id"] = id;
                ext["path"] = dir.filePath(id);
                QFile mf(dir.filePath(id) + "/manifest.json");
                if (mf.open(QIODevice::ReadOnly)) {
                    QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
                    ext["name"] = manifest["name"].toString();
                    ext["version"] = manifest["version"].toString();
                    ext["author"] = manifest["author"].toString();
                }
                arr.append(ext);
            }
            std::cout << QJsonDocument(arr).toJson(QJsonDocument::Compact).toStdString() << std::endl;
        } else {
            ExtCli::header("Installed Extensions (" + QString::number(entries.size()) + ")");
            std::cout << std::endl;

            for (const auto& id : entries) {
                QFile mf(dir.filePath(id) + "/manifest.json");
                QString name = id, version = "?", author = "?";
                if (mf.open(QIODevice::ReadOnly)) {
                    QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
                    name = manifest["name"].toString(id);
                    version = manifest["version"].toString("?");
                    author = manifest["author"].toString("?");
                }

                // Check if config exists
                bool hasConfig = QFile::exists(dir.filePath(id) + "/config.json");

                QString status = hasConfig ? "\033[92m●\033[0m" : "\033[90m○\033[0m";
                std::cout << "  " << status.toStdString() << " "
                          << name.toStdString() << " (" << id.toStdString() << ") v"
                          << version.toStdString();
                if (!author.isEmpty() && author != "?")
                    std::cout << " by " << author.toStdString();
                std::cout << std::endl;
            }
        }
        return 0;
    }
};

// ============================================================================
// ext run — Run extension from CLI
// ============================================================================

class ExtRunCommand : public CLICommand {
public:
    QString name() const override { return "ext-run"; }
    QString description() const override { return "Run a FluxScript extension from CLI."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("debug", "Enable verbose debug output"));
        parser.addOption(QCommandLineOption("args", "Arguments to pass", "args"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            ExtCli::error("Usage: viora ext-run <extension-dir> [--debug]");
            return 1;
        }

        QString extDir = args[0];
        QFile mf(extDir + "/manifest.json");
        if (!mf.open(QIODevice::ReadOnly)) {
            ExtCli::error("manifest.json not found in " + extDir);
            return 1;
        }

        QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
        QString id = manifest["id"].toString();
        QString mainFile = manifest["main"].toString("main.flux");
        QString activateHook = manifest["hooks"].toObject()["onActivate"].toString();

        ExtCli::header("Running extension: " + id);

        // Set environment variable for config functions
        qputenv("VIORA_EXTENSION_DIR", extDir.toUtf8());

        // Set up sandbox for this extension
        IDE::sandbox().setCurrentExtension(id);

        // Load permissions from manifest
        QJsonArray permArr = manifest["permissions"].toArray();
        QSet<IDE::Permission> perms;
        for (const auto& p : permArr) {
            IDE::Permission perm = IDE::permissionFromString(p.toString().trimmed().toLower());
            if (perm != IDE::Permission::None) perms.insert(perm);
        }
        IDE::sandbox().setPermissions(id, perms);

        // Initialize engine
        FluxScriptEngine::instance().initialize();

        // Read source
        QFile sf(extDir + "/" + mainFile);
        if (!sf.open(QIODevice::ReadOnly)) {
            ExtCli::error("Cannot read " + mainFile);
            return 1;
        }
        QString source = QString::fromUtf8(sf.readAll());

        // Compile
        ExtCli::info("Compiling...");
        QString error;
        if (!FluxScriptEngine::instance().executeString(source, &error)) {
            ExtCli::error("Compile error:");
            std::cerr << error.toStdString() << std::endl;
            return 1;
        }
        ExtCli::success("Compiled successfully");

        // Run top-level code
        {
            std::string anonErr;
            Flux::JITEngine::instance().callFunction("__anon_expr", {}, &anonErr);
        }

        // Call activation hook
        if (!activateHook.isEmpty()) {
            ExtCli::info("Calling " + activateHook + "...");
            QString hookError;
            FluxScriptEngine::instance().callFunction(activateHook.toUtf8().constData(), {}, &hookError);
            if (!hookError.isEmpty()) {
                ExtCli::error("Hook error: " + hookError);
                return 1;
            }
        }

        ExtCli::success("Extension '" + id + "' ran successfully");
        return 0;
    }
};

// ============================================================================
// ext validate — Validate extension
// ============================================================================

class ExtValidateCommand : public CLICommand {
public:
    QString name() const override { return "ext-validate"; }
    QString description() const override { return "Validate extension manifest and compile check."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("json", "Output as JSON"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            ExtCli::error("Usage: viora ext-validate <extension-dir>");
            return 1;
        }

        QString extDir = args[0];
        bool valid = true;
        bool jsonOutput = parser.isSet("json");
        QJsonObject result;

        // Check manifest.json
        QFile mf(extDir + "/manifest.json");
        if (mf.exists() && mf.open(QIODevice::ReadOnly)) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &err);
            mf.close();
            if (err.error != QJsonParseError::NoError) {
                ExtCli::error("manifest.json parse error: " + err.errorString());
                result["manifest"] = "invalid";
                valid = false;
            } else {
                QJsonObject obj = doc.object();
                if (obj["id"].toString().isEmpty()) {
                    ExtCli::error("manifest.json missing 'id'");
                    result["manifest"] = "missing-id";
                    valid = false;
                } else {
                    ExtCli::success("manifest.json valid (" + obj["id"].toString() + ")");
                    result["manifest"] = "valid";
                    result["id"] = obj["id"];
                    result["version"] = obj["version"];
                    QJsonObject deps = obj["dependencies"].toObject();
                    if (!deps.isEmpty()) {
                        ExtCli::info("Dependencies: " + QString::number(deps.size()));
                        for (auto it = deps.constBegin(); it != deps.constEnd(); ++it)
                            ExtCli::info("  " + it.key() + " " + it.value().toString());
                    }
                }
            }
        } else {
            ExtCli::error("manifest.json not found or unreadable");
            result["manifest"] = "missing";
            valid = false;
        }

        // Check main.flux
        QFile sf(extDir + "/main.flux");
        if (sf.exists() && sf.open(QIODevice::ReadOnly)) {
            FluxScriptEngine::instance().initialize();
            QString source = QString::fromUtf8(sf.readAll());
            sf.close();
            QString error;
            if (!FluxScriptEngine::instance().executeString(source, &error)) {
                ExtCli::error("main.flux compile error:");
                std::cerr << "  " << error.toStdString() << std::endl;
                result["main"] = "compile-error";
                valid = false;
            } else {
                ExtCli::success("main.flux compiles OK");
                result["main"] = "valid";
            }
        } else {
            ExtCli::info("main.flux not found (optional)");
            result["main"] = "missing";
        }

        // Check config.json
        QFile cf(extDir + "/config.json");
        if (cf.exists()) {
            ExtCli::success("config.json exists");
            result["config"] = "present";
        } else {
            ExtCli::info("config.json not created yet (optional)");
            result["config"] = "absent";
        }

        if (jsonOutput) {
            result["valid"] = valid;
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << std::endl;
        } else {
            if (valid) ExtCli::success("Extension is valid");
            else ExtCli::error("Extension has errors");
        }

        return valid ? 0 : 1;
    }
};

// ============================================================================
// ext package — Package extension for distribution
// ============================================================================

class ExtPackageCommand : public CLICommand {
public:
    QString name() const override { return "ext-package"; }
    QString description() const override { return "Package extension into a distributable .vioraext file."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("output", "Output file path", "output"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            ExtCli::error("Usage: viora ext-package <extension-dir> [--output PATH]");
            return 1;
        }

        QString extDir = args[0];
        QFile mf(extDir + "/manifest.json");
        if (!mf.open(QIODevice::ReadOnly)) {
            ExtCli::error("manifest.json not found");
            return 1;
        }

        QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
        QString id = manifest["id"].toString();
        QString version = manifest["version"].toString();

        QString outputPath = parser.value("output");
        if (outputPath.isEmpty()) {
            outputPath = id + "-" + version + ".vioraext";
        }

        ExtCli::header("Packaging " + id + " v" + version);

        // Create package
        QJsonObject package;
        package["format"] = "vioraext-v1";
        package["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        package["manifest"] = manifest;

        // Collect all files
        QJsonObject files;
        QDir dir(extDir);
        for (const auto& f : dir.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
            QFile file(dir.filePath(f));
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray data = file.readAll();
                QJsonObject fileInfo;
                fileInfo["sizeBytes"] = static_cast<qint64>(data.size());
                fileInfo["sha256"] = QString::fromLatin1(
                    QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
                fileInfo["contentBase64"] = QString::fromLatin1(data.toBase64());
                files[f] = fileInfo;
            }
        }
        package["files"] = files;

        // Write package
        QFile outFile(outputPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            ExtCli::error("Cannot write " + outputPath);
            return 1;
        }
        outFile.write(QJsonDocument(package).toJson(QJsonDocument::Indented));

        ExtCli::success("Packaged to " + outputPath);
        ExtCli::info("Files: " + QString::number(files.size()));
        ExtCli::info("Size: " + QString::number(QFileInfo(outputPath).size()) + " bytes");
        return 0;
    }
};

// ============================================================================
// ext deps — Show dependency tree
// ============================================================================

class ExtDepsCommand : public CLICommand {
public:
    QString name() const override { return "ext-deps"; }
    QString description() const override { return "Show extension dependency tree."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("json", "Output as JSON"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        QDir dir(extensionsDir());
        if (!dir.exists()) {
            ExtCli::info("No extensions installed");
            return 0;
        }

        // Build dependency graph
        QMap<QString, QMap<QString, QString>> allDeps;
        QMap<QString, QString> allVersions;

        auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto& id : entries) {
            QFile mf(dir.filePath(id) + "/manifest.json");
            if (!mf.open(QIODevice::ReadOnly)) continue;
            QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
            allVersions[id] = manifest["version"].toString();
            QJsonObject deps = manifest["dependencies"].toObject();
            QMap<QString, QString> depMap;
            for (auto it = deps.constBegin(); it != deps.constEnd(); ++it) {
                depMap[it.key()] = it.value().toString();
            }
            allDeps[id] = depMap;
        }

        bool jsonOutput = parser.isSet("json");
        if (jsonOutput) {
            QJsonObject result;
            for (auto it = allDeps.constBegin(); it != allDeps.constEnd(); ++it) {
                QJsonObject depObj;
                for (auto dit = it.value().constBegin(); dit != it.value().constEnd(); ++dit) {
                    depObj[dit.key()] = dit.value();
                }
                result[it.key()] = depObj;
            }
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << std::endl;
        } else {
            ExtCli::header("Extension Dependencies");
            std::cout << std::endl;

            for (auto it = allDeps.constBegin(); it != allDeps.constEnd(); ++it) {
                std::cout << "  " << it.key().toStdString() << " v"
                          << allVersions[it.key()].toStdString();
                if (it.value().isEmpty()) {
                    std::cout << " (no dependencies)" << std::endl;
                } else {
                    std::cout << std::endl;
                    for (auto dit = it.value().constBegin(); dit != it.value().constEnd(); ++dit) {
                        QString status = allVersions.contains(dit.key()) ? "\033[92minstalled\033[0m" : "\033[91mmissing\033[0m";
                        std::cout << "    └─ " << dit.key().toStdString() << " " << dit.value().toStdString()
                                  << " [" << status.toStdString() << "]" << std::endl;
                    }
                }
            }
        }
        return 0;
    }
};

// ============================================================================
// ext config — Manage extension configuration
// ============================================================================

class ExtConfigCommand : public CLICommand {
public:
    QString name() const override { return "ext-config"; }
    QString description() const override { return "Read/write extension configuration."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("json", "Output as JSON"));
        parser.addOption(QCommandLineOption("list", "List all config keys"));
        parser.addOption(QCommandLineOption("reset", "Reset config to defaults"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 1) {
            ExtCli::error("Usage: viora ext-config <extension-id> [key] [value]");
            ExtCli::info("  viora ext-config <id> --list         List all config");
            ExtCli::info("  viora ext-config <id> <key>          Get config value");
            ExtCli::info("  viora ext-config <id> <key> <value>  Set config value");
            ExtCli::info("  viora ext-config <id> --reset        Reset config");
            return 1;
        }

        QString extId = args[0];
        QString configPath = extensionsDir() + "/" + extId + "/config.json";

        if (parser.isSet("reset")) {
            QFile::remove(configPath);
            ExtCli::success("Config reset for " + extId);
            return 0;
        }

        // Load config
        QJsonObject config;
        QFile cf(configPath);
        if (cf.open(QIODevice::ReadOnly)) {
            config = QJsonDocument::fromJson(cf.readAll()).object();
        }

        if (parser.isSet("list") || args.size() == 1) {
            bool jsonOutput = parser.isSet("json");
            if (jsonOutput) {
                std::cout << QJsonDocument(config).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            } else {
                ExtCli::header("Config for " + extId);
                if (config.isEmpty()) {
                    ExtCli::info("No config set");
                } else {
                    for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
                        std::cout << "  " << it.key().toStdString() << " = "
                                  << it.value().toVariant().toString().toStdString() << std::endl;
                    }
                }
            }
            return 0;
        }

        if (args.size() == 2) {
            // Get value
            QString key = args[1];
            QJsonValue val = config.value(key);
            if (val.isUndefined()) {
                printWarn("Key '" + key + "' not found");
                return 1;
            }
            std::cout << val.toVariant().toString().toStdString() << std::endl;
            return 0;
        }

        if (args.size() >= 3) {
            // Set value
            QString key = args[1];
            QString value = args[2];

            // Try to parse as number
            bool ok;
            double numVal = value.toDouble(&ok);
            if (ok) {
                config[key] = numVal;
            } else if (value == "true") {
                config[key] = true;
            } else if (value == "false") {
                config[key] = false;
            } else {
                config[key] = value;
            }

            QFile outFile(configPath);
            outFile.open(QIODevice::WriteOnly);
            outFile.write(QJsonDocument(config).toJson(QJsonDocument::Indented));

            ExtCli::success("Set " + key + " = " + value);
            return 0;
        }

        return 0;
    }
};

// ============================================================================
// ext logs — Show extension logs
// ============================================================================

class ExtLogsCommand : public CLICommand {
public:
    QString name() const override { return "ext-logs"; }
    QString description() const override { return "View extension debug logs."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("follow", "Follow log output (like tail -f)"));
        parser.addOption(QCommandLineOption("lines", "Number of lines to show", "lines", "50"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        QString logFile = logDir() + "/vioraide.log";
        if (!QFile::exists(logFile)) {
            ExtCli::info("No logs found. Run an extension first.");
            return 0;
        }

        int numLines = parser.value("lines").toInt();

        QFile f(logFile);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            ExtCli::error("Cannot read log file");
            return 1;
        }

        QTextStream stream(&f);
        QStringList lines;
        while (!stream.atEnd()) {
            lines.append(stream.readLine());
        }

        // Show last N lines
        int start = qMax(0, lines.size() - numLines);
        for (int i = start; i < lines.size(); ++i) {
            QString line = lines[i];
            if (line.contains("[ERROR]") || line.contains("error")) {
                printColor("  " + line, "\033[91m");
            } else if (line.contains("[WARN]") || line.contains("warning")) {
                printColor("  " + line, "\033[93m");
            } else if (line.contains("[DEBUG]")) {
                printColor("  " + line, "\033[90m");
            } else {
                std::cout << "  " << line.toStdString() << std::endl;
            }
        }

        // Follow mode
        if (parser.isSet("follow")) {
            ExtCli::info("Following log output (Ctrl+C to stop)...");
            QFile followFile(logFile);
            followFile.seek(followFile.size());
            while (true) {
                QByteArray line = followFile.readLine();
                if (!line.isEmpty()) {
                    std::cout << line.toStdString();
                }
                QCoreApplication::processEvents();
                QThread::msleep(100);
            }
        }

        return 0;
    }
};

// ============================================================================
// Registration
// ============================================================================

void registerExtCommands() {
    auto& reg = CommandRegistry::instance();
    reg.registerCommand(std::make_unique<ExtCreateCommand>());
    reg.registerCommand(std::make_unique<ExtListCommand>());
    reg.registerCommand(std::make_unique<ExtRunCommand>());
    reg.registerCommand(std::make_unique<ExtValidateCommand>());
    reg.registerCommand(std::make_unique<ExtPackageCommand>());
    reg.registerCommand(std::make_unique<ExtDepsCommand>());
    reg.registerCommand(std::make_unique<ExtConfigCommand>());
    reg.registerCommand(std::make_unique<ExtLogsCommand>());
}
