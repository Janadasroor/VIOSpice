/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "plugin_commands.h"
#include "common.h"
#include "../command_registry.h"

#include "simulator/bridge/slang_manager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <iostream>

namespace {

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

class PluginPackCommand : public CLICommand {
public:
    QString name() const override { return "plugin-pack"; }
    QString description() const override { return "Package manifest and artifact into a .fluxplugin."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"manifest.json", "artifact-file", "output.fluxplugin"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"ok", "bool"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.size() < 3) {
            std::cerr << "Usage: viora plugin-pack <manifest.json> <artifact-file> <output.fluxplugin>" << std::endl;
            return 1;
        }

        const QString manifestPath = args.at(0);
        const QString artifactPath = args.at(1);
        const QString outputPath = args.at(2);

        QFile manifestFile(manifestPath);
        if (!manifestFile.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Cannot read manifest: " << manifestPath.toStdString() << std::endl;
            return 1;
        }
        const QByteArray manifestBytes = manifestFile.readAll();
        manifestFile.close();

        QJsonParseError parseError;
        const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject()) {
            std::cerr << "Error: Invalid manifest JSON: " << parseError.errorString().toStdString() << std::endl;
            return 1;
        }
        const QJsonObject manifest = manifestDoc.object();
        const QString pluginId = manifest.value("id").toString().trimmed();
        const QString version = manifest.value("version").toString().trimmed();
        if (pluginId.isEmpty() || version.isEmpty()) {
            std::cerr << "Error: manifest must contain non-empty id and version fields." << std::endl;
            return 1;
        }

        QFile artifactFile(artifactPath);
        if (!artifactFile.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Cannot read artifact: " << artifactPath.toStdString() << std::endl;
            return 1;
        }
        const QByteArray artifactBytes = artifactFile.readAll();
        artifactFile.close();

        const QString artifactSha = sha256Hex(artifactBytes);
        const QString artifactName = QFileInfo(artifactPath).fileName();

        QJsonObject payload;
        payload["format"] = "fluxplugin-v1";
        payload["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        payload["manifest"] = manifest;

        QJsonObject artifact;
        artifact["name"] = artifactName;
        artifact["sizeBytes"] = static_cast<qint64>(artifactBytes.size());
        artifact["sha256"] = artifactSha;
        artifact["contentBase64"] = QString::fromLatin1(artifactBytes.toBase64());
        payload["artifact"] = artifact;

        const QJsonDocument outDoc(payload);
        QFile outFile(outputPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::cerr << "Error: Cannot write output package: " << outputPath.toStdString() << std::endl;
            return 1;
        }
        outFile.write(outDoc.toJson(QJsonDocument::Indented));
        outFile.close();

        if (!g_quiet) {
            std::cout << "Packed plugin:" << std::endl;
            std::cout << "  ID: " << pluginId.toStdString() << std::endl;
            std::cout << "  Version: " << version.toStdString() << std::endl;
            std::cout << "  Artifact: " << artifactName.toStdString() << " (" << artifactBytes.size() << " bytes)" << std::endl;
            std::cout << "  SHA-256: " << artifactSha.toStdString() << std::endl;
            std::cout << "  Output: " << outputPath.toStdString() << std::endl;
        }
        return 0;
    }
};

class PluginInspectCommand : public CLICommand {
public:
    QString name() const override { return "plugin-inspect"; }
    QString description() const override { return "Inspect package metadata and verify SHA-256."; }
    void setupParser(QCommandLineParser& parser) override {}
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"package.fluxplugin"}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"ok", "bool"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora plugin-inspect <package.fluxplugin>" << std::endl;
            return 1;
        }
        const QString packagePath = args.at(0);

        QFile packageFile(packagePath);
        if (!packageFile.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Cannot read package: " << packagePath.toStdString() << std::endl;
            return 1;
        }
        const QByteArray bytes = packageFile.readAll();
        packageFile.close();

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            std::cerr << "Error: Invalid package JSON: " << parseError.errorString().toStdString() << std::endl;
            return 1;
        }

        const QJsonObject root = doc.object();
        const QString format = root.value("format").toString();
        const QJsonObject manifest = root.value("manifest").toObject();
        const QJsonObject artifact = root.value("artifact").toObject();

        const QString pluginId = manifest.value("id").toString();
        const QString version = manifest.value("version").toString();
        const QString artifactName = artifact.value("name").toString();
        const qint64 sizeBytes = static_cast<qint64>(artifact.value("sizeBytes").toDouble(0.0));
        const QString expectedSha = artifact.value("sha256").toString().toLower();
        const QByteArray content = QByteArray::fromBase64(artifact.value("contentBase64").toString().toLatin1());
        const QString actualSha = sha256Hex(content);

        if (!g_quiet) {
            std::cout << "Package: " << packagePath.toStdString() << std::endl;
            std::cout << "  Format: " << format.toStdString() << std::endl;
            std::cout << "  Plugin ID: " << pluginId.toStdString() << std::endl;
            std::cout << "  Version: " << version.toStdString() << std::endl;
            std::cout << "  Artifact: " << artifactName.toStdString() << std::endl;
            std::cout << "  Declared Size: " << sizeBytes << std::endl;
            std::cout << "  Extracted Size: " << content.size() << std::endl;
            std::cout << "  Declared SHA-256: " << expectedSha.toStdString() << std::endl;
            std::cout << "  Actual SHA-256:   " << actualSha.toStdString() << std::endl;

            if (expectedSha == actualSha) {
                std::cout << "  Integrity check:  PASSED" << std::endl;
            } else {
                std::cerr << "  Integrity check:  FAILED (checksum mismatch)" << std::endl;
                return 1;
            }
        }
        return expectedSha == actualSha ? 0 : 1;
    }
};

class VerilogInspectCommand : public CLICommand {
public:
    QString name() const override { return "verilog-inspect"; }
    QString description() const override { return "Inspect ports and modules of a Verilog source file."; }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("module", "Module name to inspect", "modname"));
        parser.addOption(QCommandLineOption("json", "Output results in JSON format"));
    }
    QJsonObject inputSchema() const override {
        return QJsonObject{{"args", QJsonArray{"file.v"}}, {"options", QJsonObject{{"module", "string"}, {"json", "bool"}}}};
    }
    QJsonObject outputSchema() const override {
        return QJsonObject{{"file", "string"}, {"module", "string"}, {"ports", "array[port]"}};
    }
    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora verilog-inspect <file.v> [options]" << std::endl;
            return 1;
        }
        const QString filePath = args.at(0);
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::cerr << "Error: Cannot open Verilog file: " << filePath.toStdString() << std::endl;
            return 1;
        }
        QString source = QString::fromUtf8(file.readAll());
        file.close();

        QString error;
        QString moduleName = parser.value("module");
        if (moduleName.isEmpty()) {
            static const QRegularExpression modRe(R"(\bmodule\s+(\w+))");
            auto match = modRe.match(source);
            if (match.hasMatch()) moduleName = match.captured(1);
        }

        auto ports = SlangManager::instance().extractPorts(source, moduleName, &error);
        if (!error.isEmpty() && ports.isEmpty()) {
            std::cerr << "Slang Error: " << error.toStdString() << std::endl;
            return 1;
        }

        if (parser.isSet("json")) {
            QJsonObject root;
            root["file"] = filePath;
            root["module"] = moduleName;
            QJsonArray portArray;
            for (const auto& p : ports) {
                QJsonObject po;
                po["name"] = p.name;
                po["width"] = p.width;
                po["direction"] = p.isInput ? "input" : "output";
                portArray.append(po);
            }
            root["ports"] = portArray;
            if (!error.isEmpty()) root["warnings"] = error;
            printJsonValue(root);
        } else {
            std::cout << "File: " << filePath.toStdString() << "\n";
            std::cout << "Module: " << moduleName.toStdString() << "\n";
            std::cout << "Ports:\n";
            for (const auto& p : ports) {
                std::cout << "  " << (p.isInput ? "input " : "output") << " [" << p.width << "] " << p.name.toStdString() << "\n";
            }
            if (!error.isEmpty()) std::cout << "\nWarnings/Errors:\n" << error.toStdString() << "\n";
        }

        return 0;
    }
};

} // namespace

void registerPluginCommands() {
    auto& reg = CommandRegistry::instance();
    reg.registerCommand(std::make_unique<PluginPackCommand>());
    reg.registerCommand(std::make_unique<PluginInspectCommand>());
    reg.registerCommand(std::make_unique<VerilogInspectCommand>());
}
