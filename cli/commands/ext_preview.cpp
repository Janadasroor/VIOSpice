/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

// ext_preview.cpp — Preview extension GUI with screenshot capture
// Usage: viora ext-preview <extension-dir> [--output DIR] [--delay MS]

#include "ext_preview.h"
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
#include <QTimer>
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QDialog>
#include <QScreen>
#include <QPixmap>
#include <QPainter>
#include <QThread>

#include "../core/flux/engine/flux_script_engine.h"
#include "../core/flux/extensions/extension_manager.h"
#include "../core/flux/extensions/extension_sandbox.h"
#include "../core/flux/extensions/extension_config.h"
// FluxScript's bundled headers are not self-contained (compiler_instance.h uses
// std::unordered_set without including it); declare it before parsing flux headers.
#include <unordered_set>
#include <flux/jit_engine.h>

class ExtPreviewCommand : public CLICommand {
public:
    QString name() const override { return "ext-preview"; }
    QString description() const override {
        return "Preview extension GUI and capture screenshots.";
    }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("output", "Output directory for screenshots", "output", QDir::tempPath() + "/ext-preview"));
        parser.addOption(QCommandLineOption("delay", "Delay before capture (ms)", "delay", "500"));
        parser.addOption(QCommandLineOption("json", "Output results as JSON"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }

    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora ext-preview <extension-dir> [--output DIR] [--delay MS]\n";
            return 1;
        }

        QString extDir = args[0];
        QString outputDir = parser.value("output");
        int delay = parser.value("delay").toInt();
        bool jsonOutput = parser.isSet("json");

        if (!QDir(extDir).exists()) {
            std::cerr << "Error: directory not found: " << extDir.toStdString() << "\n";
            return 1;
        }

        // Read manifest
        QFile mf(extDir + "/manifest.json");
        if (!mf.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: manifest.json not found\n";
            return 1;
        }

        QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
        QString id = manifest["id"].toString();
        QString mainFile = manifest["main"].toString("main.flux");
        QString activateHook = manifest["hooks"].toObject()["onActivate"].toString();

        if (!jsonOutput) {
            std::cout << "Previewing extension: " << id.toStdString() << "\n";
            std::cout << "Output: " << outputDir.toStdString() << "\n\n";
        }

        // Create output directory
        QDir().mkpath(outputDir);

        // Set sandbox permissions
        QJsonArray permArr = manifest["permissions"].toArray();
        QSet<IDE::Permission> perms;
        for (const auto& p : permArr) {
            IDE::Permission perm = IDE::permissionFromString(p.toString().trimmed().toLower());
            if (perm != IDE::Permission::None) perms.insert(perm);
        }
        IDE::sandbox().setPermissions(id, perms);
        IDE::sandbox().setCurrentExtension(id);

        // Set config dir env
        qputenv("VIORA_EXTENSION_DIR", extDir.toUtf8());

        // Initialize engine
        FluxScriptEngine::instance().initialize();

        // Read and compile source
        QFile sf(extDir + "/" + mainFile);
        if (!sf.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: cannot read " << mainFile.toStdString() << "\n";
            return 1;
        }

        QString source = QString::fromUtf8(sf.readAll());
        QString error;
        if (!FluxScriptEngine::instance().executeString(source, &error)) {
            std::cerr << "Error: compile failed: " << error.toStdString() << "\n";
            return 1;
        }

        // Execute top-level code (anonymous expressions)
        {
            std::string anonErr;
            Flux::JITEngine::instance().callFunction("__anon_expr", {}, &anonErr);
        }

        if (!jsonOutput) std::cout << "Compiled successfully\n";

        // Run activation hook and open_panel
        if (!activateHook.isEmpty()) {
            QString hookError;
            FluxScriptEngine::instance().callFunction(
                activateHook.toUtf8().constData(), {}, &hookError);
            if (!hookError.isEmpty()) {
                std::cerr << "Warning: hook error: " << hookError.toStdString() << "\n";
            }
        }

        // Also call open_panel if it exists
        {
            QString panelError;
            FluxScriptEngine::instance().callFunction("open_panel", {}, &panelError);
            // Ignore error - open_panel might not exist
        }

        // Wait for GUI to render
        for (int i = 0; i < 10; ++i) {
            QCoreApplication::processEvents();
            QThread::msleep(100);
        }
        QCoreApplication::processEvents();

        // Capture all visible QDialogs (extension windows)
        int screenshotCount = 0;
        QList<QDialog*> dialogs;
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (QDialog* dialog = qobject_cast<QDialog*>(widget)) {
                if (dialog->isVisible()) {
                    dialogs.append(dialog);
                }
            }
        }

        // Also check for any visible QWidgets with content
        if (dialogs.isEmpty()) {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (widget->isVisible() && widget->width() > 50 && widget->height() > 50) {
                    QString name = widget->windowTitle();
                    if (name.isEmpty()) name = widget->objectName();
                    if (!name.isEmpty()) {
                        QString cleanName = name;
                        cleanName.replace(" ", "_");
                        cleanName.remove(QRegularExpression("[^a-zA-Z0-9_-]"));
                        QString filePath = outputDir + "/" + cleanName + ".png";
                        QPixmap pixmap = widget->grab();
                        pixmap.save(filePath, "PNG");
                        if (!jsonOutput) {
                            std::cout << "  Captured: " << filePath.toStdString()
                                      << " (" << pixmap.width() << "x" << pixmap.height() << ")\n";
                        }
                        screenshotCount++;
                    }
                }
            }
        }

        // Capture each dialog
        for (QDialog* dialog : dialogs) {
            QString name = dialog->windowTitle();
            if (name.isEmpty()) name = dialog->objectName();
            if (name.isEmpty()) name = QString("dialog_%1").arg(screenshotCount);

            QString cleanName = name;
            cleanName.replace(" ", "_");
            cleanName.remove(QRegularExpression("[^a-zA-Z0-9_-]"));

            QString filePath = outputDir + "/" + cleanName + ".png";
            QPixmap pixmap = dialog->grab();
            pixmap.save(filePath, "PNG");

            if (!jsonOutput) {
                std::cout << "  Captured: " << filePath.toStdString()
                          << " (" << pixmap.width() << "x" << pixmap.height() << ")\n";
            }
            screenshotCount++;
        }

        // If still no captures, try grab from primary screen
        if (screenshotCount == 0) {
            QScreen* screen = QApplication::primaryScreen();
            if (screen) {
                QPixmap pixmap = screen->grabWindow(0);
                QString filePath = outputDir + "/full_screen.png";
                pixmap.save(filePath, "PNG");
                if (!jsonOutput) {
                    std::cout << "  Captured full screen: " << filePath.toStdString() << "\n";
                }
                screenshotCount = 1;
            }
        }

        if (jsonOutput) {
            QJsonObject result;
            result["extensionId"] = id;
            result["outputDir"] = outputDir;
            result["screenshotCount"] = screenshotCount;
            result["screenshots"] = QJsonArray();
            QDir dir(outputDir);
            for (const auto& f : dir.entryList({"*.png"}, QDir::Files)) {
                QJsonObject ss;
                ss["file"] = f;
                ss["path"] = dir.filePath(f);
                result["screenshots"].toArray().append(ss);
            }
            std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << "\n";
        } else {
            std::cout << "\nCaptured " << screenshotCount << " screenshot(s) to " << outputDir.toStdString() << "\n";
        }

        return 0;
    }
};

void registerExtPreviewCommand() {
    CommandRegistry::instance().registerCommand(std::make_unique<ExtPreviewCommand>());
}
