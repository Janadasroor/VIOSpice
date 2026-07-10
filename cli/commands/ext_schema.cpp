/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

// ext_schema.cpp — Schema generation and validation for extension manifests
// Usage: viora ext-schema generate   — Output JSON schema for manifest.json
//        viora ext-schema validate <dir> — Validate extension against schema

#include "ext_schema.h"
#include "common.h"
#include "../command_registry.h"

#include <iostream>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

#include "../core/flux/extensions/extension_sandbox.h"

class ExtSchemaGenerateCommand : public CLICommand {
public:
    QString name() const override { return "ext-schema-generate"; }
    QString description() const override { return "Generate JSON schema for manifest.json."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("output", "Output file path", "output"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        QJsonObject schema;
        schema["$schema"] = "http://json-schema.org/draft-07/schema#";
        schema["title"] = "VioraIDE Extension Manifest";
        schema["description"] = "Schema for extension manifest.json files";
        schema["type"] = "object";

        QJsonObject properties;

        // id
        QJsonObject idProp;
        idProp["type"] = "string";
        idProp["description"] = "Unique extension identifier";
        idProp["pattern"] = "^[a-z][a-z0-9_-]*$";
        idProp["minLength"] = 1;
        idProp["maxLength"] = 64;
        properties["id"] = idProp;

        // name
        QJsonObject nameProp;
        nameProp["type"] = "string";
        nameProp["description"] = "Display name";
        properties["name"] = nameProp;

        // version
        QJsonObject versionProp;
        versionProp["type"] = "string";
        versionProp["description"] = "Semantic version (e.g., 1.0.0)";
        versionProp["pattern"] = "^\\d+\\.\\d+\\.\\d+$";
        properties["version"] = versionProp;

        // author
        QJsonObject authorProp;
        authorProp["type"] = "string";
        authorProp["description"] = "Author name";
        properties["author"] = authorProp;

        // description
        QJsonObject descProp;
        descProp["type"] = "string";
        descProp["description"] = "Extension description";
        properties["description"] = descProp;

        // main
        QJsonObject mainProp;
        mainProp["type"] = "string";
        mainProp["description"] = "Main FluxScript file";
        mainProp["default"] = "main.flux";
        properties["main"] = mainProp;

        // hooks
        QJsonObject hooksProp;
        hooksProp["type"] = "object";
        hooksProp["description"] = "Lifecycle hooks";
        QJsonObject hooksProps;
        QJsonObject onActivate;
        onActivate["type"] = "string";
        onActivate["description"] = "Function called on activation";
        hooksProps["onActivate"] = onActivate;
        QJsonObject onDeactivate;
        onDeactivate["type"] = "string";
        onDeactivate["description"] = "Function called on deactivation";
        hooksProps["onDeactivate"] = onDeactivate;
        hooksProp["properties"] = hooksProps;
        properties["hooks"] = hooksProp;

        // menu
        QJsonObject menuProp;
        menuProp["type"] = "array";
        menuProp["description"] = "Menu entries";
        QJsonObject menuItems;
        menuItems["type"] = "object";
        QJsonObject menuProps;
        QJsonObject mpPath;
        mpPath[("type")] = "string";
        mpPath[("description")] = "Menu path (e.g., 'Tools/My Extension')";
        menuProps["path"] = mpPath;
        QJsonObject mpAction;
        mpAction["type"] = "string";
        mpAction["description"] = "Function to call";
        menuProps["action"] = mpAction;
        QJsonObject mpIcon;
        mpIcon["type"] = "string";
        mpIcon["description"] = "Icon filename (optional)";
        menuProps["icon"] = mpIcon;
        menuItems["properties"] = menuProps;
        menuItems["required"] = QJsonArray{"path", "action"};
        menuProp["items"] = menuItems;
        properties["menu"] = menuProp;

        // contexts
        QJsonObject ctxProp;
        ctxProp["type"] = "array";
        ctxProp["description"] = "Component type handlers";
        QJsonObject ctxItems;
        ctxItems["type"] = "object";
        QJsonObject ctxProps;
        QJsonObject cpType;
        cpType["type"] = "string";
        cpType["description"] = "Component type (e.g., 'R', 'C', 'V')";
        ctxProps["componentType"] = cpType;
        QJsonObject cpAction;
        cpAction["type"] = "string";
        cpAction["description"] = "Function to call on double-click";
        ctxProps["action"] = cpAction;
        ctxItems["properties"] = ctxProps;
        ctxItems["required"] = QJsonArray{"componentType", "action"};
        ctxProp["items"] = ctxItems;
        properties["contexts"] = ctxProp;

        // permissions
        QJsonObject permProp;
        permProp["type"] = "array";
        permProp["description"] = "Sandbox permissions";
        QJsonObject permItems;
        permItems["type"] = "string";
        QJsonArray permValues;
        auto perms = IDE::ExtensionSandbox::availablePermissions();
        for (auto it = perms.constBegin(); it != perms.constEnd(); ++it) {
            permValues.append(IDE::permissionToString(it.key()));
        }
        permItems["enum"] = permValues;
        permProp["items"] = permItems;
        properties["permissions"] = permProp;

        // dependencies
        QJsonObject depProp;
        depProp["type"] = "object";
        depProp["description"] = "Extension dependencies (id -> version constraint)";
        depProp["additionalProperties"] = QJsonObject{{"type", "string"}};
        properties["dependencies"] = depProp;

        schema["properties"] = properties;
        schema["required"] = QJsonArray{"id", "version", "main"};

        QString output = parser.value("output");
        if (output.isEmpty()) {
            std::cout << QJsonDocument(schema).toJson(QJsonDocument::Indented).toStdString() << std::endl;
        } else {
            QFile f(output);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(schema).toJson(QJsonDocument::Indented));
                std::cout << "Schema written to " << output.toStdString() << std::endl;
            } else {
                std::cerr << "Cannot write to " << output.toStdString() << std::endl;
                return 1;
            }
        }
        return 0;
    }
};

class ExtSchemaValidateCommand : public CLICommand {
public:
    QString name() const override { return "ext-schema-validate"; }
    QString description() const override { return "Validate extension against manifest schema."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("fix", "Attempt to auto-fix issues"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora ext-schema-validate <extension-dir> [--fix]\n";
            return 1;
        }

        QString extDir = args[0];
        bool autoFix = parser.isSet("fix");

        QFile mf(extDir + "/manifest.json");
        if (!mf.exists()) {
            std::cerr << "Error: manifest.json not found\n";
            return 1;
        }

        if (!mf.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: cannot read manifest.json\n";
            return 1;
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(mf.readAll(), &err);
        mf.close();

        if (err.error != QJsonParseError::NoError) {
            std::cerr << "Error: JSON parse error at offset " << err.offset
                      << ": " << err.errorString().toStdString() << "\n";
            return 1;
        }

        QJsonObject manifest = doc.object();
        int errors = 0;
        bool modified = false;

        // Validate required fields
        if (manifest["id"].toString().isEmpty()) {
            std::cerr << "Error: missing required field 'id'\n";
            errors++;
        }

        if (manifest["version"].toString().isEmpty()) {
            std::cerr << "Error: missing required field 'version'\n";
            errors++;
            if (autoFix) {
                manifest["version"] = "0.1.0";
                modified = true;
                std::cout << "Fixed: set version to '0.1.0'\n";
            }
        }

        if (manifest["main"].toString().isEmpty()) {
            std::cerr << "Error: missing required field 'main'\n";
            errors++;
            if (autoFix) {
                manifest["main"] = "main.flux";
                modified = true;
                std::cout << "Fixed: set main to 'main.flux'\n";
            }
        }

        // Validate id format
        QString id = manifest["id"].toString();
        if (!id.isEmpty() && !id.contains(QRegularExpression("^[a-z][a-z0-9_-]*$"))) {
            std::cerr << "Warning: id '" << id.toStdString() << "' should be lowercase alphanumeric with _ or -\n";
        }

        // Validate version format
        QString version = manifest["version"].toString();
        if (!version.isEmpty() && !version.contains(QRegularExpression("^\\d+\\.\\d+\\.\\d+$"))) {
            std::cerr << "Warning: version '" << version.toStdString() << "' should be semver (e.g., 1.0.0)\n";
        }

        // Validate hooks
        QJsonObject hooks = manifest["hooks"].toObject();
        for (auto it = hooks.constBegin(); it != hooks.constEnd(); ++it) {
            if (!it.value().isString()) {
                std::cerr << "Error: hooks." << it.key().toStdString() << " must be a string\n";
                errors++;
            }
        }

        // Validate menu
        QJsonArray menu = manifest["menu"].toArray();
        for (int i = 0; i < menu.size(); ++i) {
            QJsonObject item = menu[i].toObject();
            if (item["path"].toString().isEmpty()) {
                std::cerr << "Error: menu[" << i << "] missing 'path'\n";
                errors++;
            }
            if (item["action"].toString().isEmpty()) {
                std::cerr << "Error: menu[" << i << "] missing 'action'\n";
                errors++;
            }
        }

        // Validate permissions
        QJsonArray perms = manifest["permissions"].toArray();
        for (const auto& p : perms) {
            QString perm = p.toString();
            if (IDE::permissionFromString(perm) == IDE::Permission::None) {
                std::cerr << "Warning: unknown permission '" << perm.toStdString() << "'\n";
            }
        }

        // Validate dependencies
        QJsonObject deps = manifest["dependencies"].toObject();
        for (auto it = deps.constBegin(); it != deps.constEnd(); ++it) {
            if (!it.value().isString()) {
                std::cerr << "Error: dependency '" << it.key().toStdString() << "' version must be a string\n";
                errors++;
            }
        }

        // Validate main file exists
        QString mainFile = manifest["main"].toString("main.flux");
        if (!QFile::exists(extDir + "/" + mainFile)) {
            std::cerr << "Warning: main file '" << mainFile.toStdString() << "' not found\n";
        }

        // Auto-fix and save
        if (autoFix && modified) {
            QFile f(extDir + "/manifest.json");
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
                std::cout << "Manifest updated with fixes\n";
            }
        }

        if (errors == 0) {
            std::cout << "Manifest is valid\n";
        } else {
            std::cerr << errors << " error(s) found\n";
        }

        return errors == 0 ? 0 : 1;
    }
};

void registerExtSchemaCommand() {
    auto& reg = CommandRegistry::instance();
    reg.registerCommand(std::make_unique<ExtSchemaGenerateCommand>());
    reg.registerCommand(std::make_unique<ExtSchemaValidateCommand>());
}
