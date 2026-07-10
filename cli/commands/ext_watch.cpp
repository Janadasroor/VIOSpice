/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

// ext_watch.cpp — Live reload for extensions
// Usage: viora ext-watch <extension-dir> [--verbose]

#include "ext_watch.h"
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
#include <QDebug>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QTextStream>
#include <QThread>

#include "../core/flux/engine/flux_script_engine.h"
#include "../core/flux/extensions/extension_manager.h"
#include "../core/flux/extensions/extension_sandbox.h"
#include "../core/flux/extensions/extension_config.h"

class ExtWatchCommand : public CLICommand {
public:
    QString name() const override { return "ext-watch"; }
    QString description() const override {
        return "Watch extension files and auto-reload on changes.";
    }
    void setupParser(QCommandLineParser& parser) override {
        parser.addOption(QCommandLineOption("verbose", "Show detailed output"));
    }
    QJsonObject inputSchema() const override { return {}; }
    QJsonObject outputSchema() const override { return {}; }

    int execute(const QStringList& args, const QCommandLineParser& parser) override {
        if (args.isEmpty()) {
            std::cerr << "Usage: viora ext-watch <extension-dir> [--verbose]\n";
            return 1;
        }

        QString extDir = args[0];
        bool verbose = parser.isSet("verbose");

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
        mf.close();
        QString id = manifest["id"].toString();
        QString mainFile = manifest["main"].toString("main.flux");

        std::cout << "Watching extension: " << id.toStdString() << "\n";
        std::cout << "Files: " << mainFile.toStdString() << ", manifest.json\n";
        std::cout << "Press Ctrl+C to stop\n\n";

        // Set up file watcher
        QFileSystemWatcher watcher;
        QStringList watchFiles;
        watchFiles << extDir + "/" << mainFile << extDir + "/manifest.json";
        watcher.addPaths(watchFiles);

        // Track last modification times
        QMap<QString, QDateTime> lastModified;
        for (const auto& f : watchFiles) {
            lastModified[f] = QFileInfo(f).lastModified();
        }

        // Debounce timer
        QTimer* debounce = new QTimer();
        debounce->setSingleShot(true);
        debounce->setInterval(300);

        bool needsReload = false;

        QObject::connect(&watcher, &QFileSystemWatcher::fileChanged, [&](const QString& path) {
            if (verbose) std::cout << "File changed: " << QFileInfo(path).fileName().toStdString() << "\n";
            needsReload = true;
            debounce->start();
        });

        QObject::connect(debounce, &QTimer::timeout, [&]() {
            if (!needsReload) return;
            needsReload = false;

            std::cout << "\nReloading extension...\n";

            // Re-read manifest
            QFile mf2(extDir + "/manifest.json");
            if (mf2.open(QIODevice::ReadOnly)) {
                manifest = QJsonDocument::fromJson(mf2.readAll()).object();
                mf2.close();
            }

            // Re-initialize engine
            FluxScriptEngine::instance().finalize();
            FluxScriptEngine::instance().initialize();

            // Read and compile
            QFile sf(extDir + "/" + mainFile);
            if (!sf.open(QIODevice::ReadOnly)) {
                std::cerr << "Error: cannot read " << mainFile.toStdString() << "\n";
                return;
            }

            QString source = QString::fromUtf8(sf.readAll());
            QString error;
            if (!FluxScriptEngine::instance().executeString(source, &error)) {
                std::cerr << "Compile error: " << error.toStdString() << "\n";
                return;
            }

            std::cout << "Compiled successfully\n";

            // Run activation
            QString activateHook = manifest["hooks"].toObject()["onActivate"].toString();
            if (!activateHook.isEmpty()) {
                QString hookError;
                FluxScriptEngine::instance().callFunction(
                    activateHook.toUtf8().constData(), {}, &hookError);
                if (!hookError.isEmpty()) {
                    std::cerr << "Hook error: " << hookError.toStdString() << "\n";
                }
            }

            std::cout << "Extension reloaded\n";

            // Re-watch files (in case manifest changed)
            watcher.removePaths(watcher.files());
            watchFiles.clear();
            watchFiles << extDir + "/" + manifest["main"].toString("main.flux")
                       << extDir + "/manifest.json";
            watcher.addPaths(watchFiles);
        });

        // Process events until interrupted
        while (true) {
            QCoreApplication::processEvents();
            QThread::msleep(100);
        }

        return 0;
    }
};

void registerExtWatchCommand() {
    CommandRegistry::instance().registerCommand(std::make_unique<ExtWatchCommand>());
}
