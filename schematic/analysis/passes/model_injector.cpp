/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "model_injector.h"
#include "../spice_netlist_generator.h"
#include <QFileInfo>
#include <QDir>

void ModelInjector::inject(const QSet<QString>& includePaths,
                           const QSet<QString>& libPaths,
                           const QStringList& embeddedModelLines,
                           const QMap<QString, QString>& embeddedSubcircuits,
                           const QString& projectDir,
                           const QSet<QString>& userDeclaredModelFiles,
                           QString& netlist) {
    // Write .include and .lib directives (subcircuit/model files from symbol metadata)
    if (!includePaths.isEmpty() || !libPaths.isEmpty()) {
        QStringList includeList = includePaths.values();
        includeList.sort();
        QStringList libList = libPaths.values();
        libList.sort();

        netlist += "* Model Includes\n";
        QSet<QString> emittedModelFiles = userDeclaredModelFiles;
        auto processPath = [&](const QString& inc, const QString& directive) {
            QString resolvedPath = SpiceNetlistGenerator::normalizeIncludePathForNetlist(inc, projectDir);
            if (resolvedPath.isEmpty()) return;

            if (emittedModelFiles.contains(resolvedPath)) return;

            QString emittedPath = resolvedPath;
            QString emittedDirective = directive;
            if (QFileInfo::exists(resolvedPath)) {
                emittedPath = SpiceNetlistGenerator::sanitizeModelIncludeForNgspice(resolvedPath);
                emittedPath = QDir::fromNativeSeparators(QDir::cleanPath(emittedPath));
                // ngspice accepts plain model/subckt files via .include. Using
                // .lib for standalone cached files causes parse failures because
                // there is no section selector.
                emittedDirective = "include";
                if (emittedModelFiles.contains(emittedPath)) return;
            }

            netlist += QString(".%1 \"%2\"\n").arg(emittedDirective, emittedPath);
            emittedModelFiles.insert(resolvedPath);
            emittedModelFiles.insert(emittedPath);
        };

        for (const QString& inc : includeList) processPath(inc, "include");
        for (const QString& lib : libList) processPath(lib, "lib");
        netlist += "\n";
    }

    // Write embedded .model lines
    if (!embeddedModelLines.isEmpty()) {
        netlist += "* Embedded Models\n";
        for (const QString& ml : embeddedModelLines) {
            netlist += ml + "\n";
        }
        netlist += "\n";
    }

    // Write embedded subcircuit definitions from symbol metadata
    if (!embeddedSubcircuits.isEmpty()) {
        netlist += "* Embedded Subcircuits (from symbol definitions)\n";
        for (auto it = embeddedSubcircuits.constBegin(); it != embeddedSubcircuits.constEnd(); ++it) {
            netlist += it.value();
            if (!it.value().endsWith('\n')) netlist += '\n';
            netlist += '\n';
        }
    }
}
