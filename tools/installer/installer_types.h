/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INSTALLER_TYPES_H
#define INSTALLER_TYPES_H

#include <QString>
#include <QStringList>
#include <cstdint>

enum class InstallType {
    Full,
    Minimal,
    Custom
};

struct ComponentSelection {
    bool coreSuite{true};        // VioraEDA.exe, DLLs, plugins, base assets
    bool simulators{true};       // ngspice, VioMATRIXC, avr_cosim, model parameters
    bool componentLibrary{true}; // ViospiceLib (symbols & footprints)
    bool cliTools{true};         // viora.exe, flux_runner.exe, flux-lsp.exe
    bool examplesAndTemplates{true}; // Circuit examples & templates
};

struct SystemIntegrationOptions {
    bool createDesktopShortcut{true};
    bool createStartMenuShortcuts{true};
    bool addToPathEnvironment{true};            // Add Viora & CLI tools (viora, vioavr, flux_runner) to PATH
    bool setupGlobalEnvironmentVariables{true};  // Set VIOSPICE_HOME, VIOAVR_HOME, FLUX_HOME system variables
    bool registerFileAssociations{true};
};

struct InstallConfig {
    QString installDir;
    InstallType installType{InstallType::Full};
    ComponentSelection components;
    SystemIntegrationOptions systemOptions;
    bool isUninstall{false};
    bool isSilent{false};
};

struct ProgressMetrics {
    int percentage{0};
    QString currentFileName;
    QString currentStatus;
    double transferSpeedMBps{0.0};
    uint64_t bytesTransferred{0};
    uint64_t totalBytes{0};
    int filesProcessed{0};
    int totalFiles{0};
    int estimatedSecondsRemaining{0};
};

#endif // INSTALLER_TYPES_H
