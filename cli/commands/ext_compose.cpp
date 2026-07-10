/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

// ext_compose.cpp — Compose extensions into new ones
// Usage: viora ext-compose <output-id> --base <ext-id> --add <ext-id> [--add <ext-id>...]

#include "ext_compose.h"
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
#include <QSet>

#include "../core/flux/extensions/extension_manager.h"

class ExtComposeCommand : public CLICommand {
public:
    QString name() const override { return "ext-compose"; }
    QString description() const override {
        return "Compose multiple extensions into a new one.";
    }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("base", "Base extension to extend", "base"));
        parser.addOption(QCommandLineOption("add", "Extension to add (can be repeated)", "add"));
        parser.addOption(QCommandLineOption("name", "Display name for composed extension", "name"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }

    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora ext-compose <output-id> --base <ext-id> --add <ext-id> [--add <ext-id>...]\n";
            return 1;
        }

        QString outputId = args[0];
        QString baseId = parser.value("base");
        QString displayName = parser.value("name");
        QStringList addIds = parser.values("add");

        if (baseId.isEmpty() && addIds.isEmpty()) {
            std::cerr << "Error: specify --base or --add\n";
            return 1;
        }

        QString extDir = QDir::homePath() + "/.config/VioraEDA/extensions";
        QString outputDir = extDir + "/" + outputId;

        if (QDir(outputDir).exists()) {
            std::cerr << "Error: extension '" << outputId.toStdString() << "' already exists\n";
            return 1;
        }

        std::cout << "Composing extension: " << outputId.toStdString() << "\n";

        // Collect source extensions
        QStringList sourceIds;
        if (!baseId.isEmpty()) sourceIds.append(baseId);
        sourceIds.append(addIds);

        // Read all manifests
        QMap<QString, QJsonObject> manifests;
        QMap<QString, QString> mainFiles;
        QMap<QString, QString> sourceDirs;
        QSet<QString> allPermissions;
        QMap<QString, QString> allDeps;

        for (const auto& id : sourceIds) {
            QString srcDir = extDir + "/" + id;
            QFile mf(srcDir + "/manifest.json");
            if (!mf.open(QIODevice::ReadOnly)) {
                std::cerr << "Error: extension '" << id.toStdString() << "' not found\n";
                return 1;
            }

            QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
            manifests[id] = manifest;
            mainFiles[id] = manifest["main"].toString("main.flux");
            sourceDirs[id] = srcDir;

            // Collect permissions
            QJsonArray perms = manifest["permissions"].toArray();
            for (const auto& p : perms) {
                allPermissions.insert(p.toString());
            }

            // Collect dependencies
            QJsonObject deps = manifest["dependencies"].toObject();
            for (auto it = deps.constBegin(); it != deps.constEnd(); ++it) {
                allDeps[it.key()] = it.value().toString();
            }

            std::cout << "  Added: " << id.toStdString() << " v"
                      << manifest["version"].toString().toStdString() << "\n";
        }

        // Create output directory
        QDir().mkpath(outputDir);

        // Generate composed manifest
        QJsonObject composedManifest;
        composedManifest["id"] = outputId;
        composedManifest["name"] = displayName.isEmpty() ? outputId : displayName;
        composedManifest["version"] = "1.0.0";
        composedManifest["author"] = "Composed from: " + sourceIds.join(", ");
        composedManifest["description"] = "Composed from " + QString::number(sourceIds.size()) + " extension(s)";
        composedManifest["main"] = "main.flux";

        QJsonObject hooks;
        hooks["onActivate"] = "init";
        composedManifest["hooks"] = hooks;

        // Combine menu entries
        QJsonArray menuArr;
        for (const auto& id : sourceIds) {
            QJsonArray srcMenu = manifests[id]["menu"].toArray();
            for (const auto& m : srcMenu) {
                QJsonObject entry = m.toObject();
                entry["path"] = id + "/" + entry["path"].toString();
                menuArr.append(entry);
            }
        }
        composedManifest["menu"] = menuArr;

        // Combine permissions
        QJsonArray permArr;
        for (const auto& p : allPermissions) {
            permArr.append(p);
        }
        composedManifest["permissions"] = permArr;

        // Combine dependencies
        QJsonObject depObj;
        for (auto it = allDeps.constBegin(); it != allDeps.constEnd(); ++it) {
            depObj[it.key()] = it.value();
        }
        composedManifest["dependencies"] = depObj;

        // Write manifest
        QFile manifestFile(outputDir + "/manifest.json");
        manifestFile.open(QIODevice::WriteOnly);
        manifestFile.write(QJsonDocument(composedManifest).toJson(QJsonDocument::Indented));

        // Generate composed main.flux
        QString composedCode = generateComposedCode(sourceIds, manifests, sourceDirs);
        QFile mainFile(outputDir + "/main.flux");
        mainFile.open(QIODevice::WriteOnly);
        mainFile.write(composedCode.toUtf8());

        // Write config.json
        QJsonObject config;
        config["composedFrom"] = QJsonArray::fromStringList(sourceIds);
        QFile configFile(outputDir + "/config.json");
        configFile.open(QIODevice::WriteOnly);
        configFile.write(QJsonDocument(config).toJson(QJsonDocument::Indented));

        std::cout << "\nComposed extension created at " << outputDir.toStdString() << "\n";
        std::cout << "  manifest.json - Combined metadata\n";
        std::cout << "  main.flux     - Merged source code\n";
        std::cout << "  config.json   - Composition metadata\n";
        std::cout << "\nRun: viora ext-run " << outputDir.toStdString() << "\n";

        return 0;
    }

private:
    QString generateComposedCode(const QStringList& sourceIds,
                                 const QMap<QString, QJsonObject>& manifests,
                                 const QMap<QString, QString>& sourceDirs) {
        QString code;
        code += "// Composed extension\n";
        code += "// Sources: " + sourceIds.join(", ") + "\n\n";

        for (const auto& id : sourceIds) {
            QString mainFile = manifests[id]["main"].toString("main.flux");
            QFile sf(sourceDirs[id] + "/" + mainFile);
            if (sf.open(QIODevice::ReadOnly)) {
                QString source = QString::fromUtf8(sf.readAll());
                code += "// === " + id + " ===\n";
                code += source;
                code += "\n\n";
            }
        }

        return code;
    }
};

void registerExtComposeCommand() {
    CommandRegistry::instance().registerCommand(std::make_unique<ExtComposeCommand>());
}
