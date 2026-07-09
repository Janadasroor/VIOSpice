/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include <QApplication>
#include <QCommandLineParser>
#include <QLoggingCategory>
#include <QFileInfo>
#include <QDir>
#include <iostream>
#include <algorithm>
#include <string>

#include "command_registry.h"
#include "commands/common.h"
#include "commands/schematic_commands.h"
#include "commands/netlist_commands.h"
#include "commands/symbol_commands.h"
#include "commands/plugin_commands.h"
#include "commands/ext_commands.h"
#include "commands/misc_commands.h"

// Register items for correct deserialization
#include "flux/schematic/factories/schematic_item_registry.h"
#include "symbols/symbol_library.h"
#include "simulator/bridge/model_library_manager.h"
#include "simulator/bridge/sim_manager.h"

#if __has_include("pcb/drc/pcb_drc.h")
#define VIOSPICE_HAS_PCB 1
#include "vioraeda/factories/pcb_item_registry.h"
#else
#define VIOSPICE_HAS_PCB 0
#endif

namespace {

void printGeneralHelp() {
    std::cout << "Usage: viora <command> [file] [options]\n\n";
    std::cout << "Available commands:\n";
    auto commands = CommandRegistry::instance().allCommands();
    for (auto* cmd : commands) {
        std::cout << "  " << cmd->name().leftJustified(22, ' ').toStdString() << cmd->description().toStdString() << "\n";
    }
    std::cout << "\nTips:\n";
    std::cout << "  Use \"viora help <command>\" for command-specific help.\n";
    std::cout << "  Use --json for machine-readable output.\n";
}

void printCommandHelp(const QString& command) {
    auto* cmd = CommandRegistry::instance().getCommand(command);
    if (cmd) {
        std::cout << cmd->name().toStdString() << " - " << cmd->description().toStdString() << "\n\n";
        QCommandLineParser parser;
        cmd->setupParser(parser);
        parser.showHelp();
    } else {
        printGeneralHelp();
    }
}

} // namespace

int main(int argc, char *argv[]) {
    // 1. Handle --help and --version before QApplication (avoids slow offscreen init)
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") {
            printGeneralHelp();
            return 0;
        }
        if (a == "--version") {
            std::cout << "viora 1.0" << std::endl;
            return 0;
        }
    }

    // 2. Decide if 'view' is requested
    bool isView = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "view") {
            isView = true;
            break;
        }
    }

#ifdef Q_OS_LINUX
    if (!isView) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#elif defined(Q_OS_WIN)
    if (!isView) {
        qputenv("QT_QPA_PLATFORM", "windows");
    }
#else
    if (!isView) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#endif

    // 3. Initialize QApplication
    QApplication app(argc, argv);
    QApplication::setApplicationName("viora");
    QCoreApplication::setApplicationVersion("1.0");

    registerAllCommands();

    // 4. Resolve subcommand
    QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        printGeneralHelp();
        return 1;
    }

    QString command = args.at(1);
    if (command == "help" || command == "--help" || command == "-h") {
        if (args.size() > 2) {
            printCommandHelp(args.at(2));
        } else {
            printGeneralHelp();
        }
        return 0;
    }

    auto* cmd = CommandRegistry::instance().getCommand(command);
    if (!cmd) {
        std::cerr << "Unknown command: " << command.toStdString() << "\n\n";
        printGeneralHelp();
        return 1;
    }

    // 5. Setup Parser and parse options
    QCommandLineParser parser;
    parser.setApplicationDescription(cmd->description());
    
    // Add global options
    QCommandLineOption jsonOption("json", "Silence non-JSON output and format results as JSON");
    QCommandLineOption quietOption("quiet", "Silence non-JSON output");
    QCommandLineOption debugOption("debug", "Enable verbose debug output");
    QCommandLineOption exitWarnOption("exit-on-warning", "Exit with non-zero code if warnings appear");
    QCommandLineOption noColorOption("no-color", "Disable colored output");
    QCommandLineOption schemaOption("schema", "Print JSON schema for the command and exit");
    QCommandLineOption helpOption(QStringList() << "h" << "help", "Show help for a command");

    parser.addOption(jsonOption);
    parser.addOption(quietOption);
    parser.addOption(debugOption);
    parser.addOption(exitWarnOption);
    parser.addOption(noColorOption);
    parser.addOption(schemaOption);
    parser.addOption(helpOption);

    // Let the subcommand setup its specific options
    cmd->setupParser(parser);

    // Parse options
    parser.process(app);

    // Handle global state variables
    g_debug = parser.isSet(debugOption);
    g_quiet = parser.isSet(quietOption);
    g_exitOnWarning = parser.isSet(exitWarnOption);
    g_noColor = parser.isSet(noColorOption);
    if (g_noColor) {
        qputenv("NO_COLOR", "1");
    }

    const bool jsonRequested = parser.isSet(jsonOption);
    if (jsonRequested) {
        g_quiet = true;
    }
    if (g_quiet || jsonRequested) {
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n"));
    }

    if (parser.isSet(helpOption)) {
        printCommandHelp(command);
        return 0;
    }

    if (parser.isSet(schemaOption)) {
        QJsonObject root;
        root["command"] = command;
        root["input"] = cmd->inputSchema();
        root["output"] = cmd->outputSchema();
        printJsonValue(root);
        return 0;
    }

    // 6. Initialize libraries and entities for CLI
#if VIOSPICE_HAS_PCB
    PCBItemRegistry::registerBuiltInItems();
#endif
    SchematicItemRegistry::registerBuiltInItems();
    
    SymbolLibraryManager::instance().loadUserLibraries(QDir::homePath() + "/ViospiceLib/sym");
    ModelLibraryManager::instance().reload();

    // 7. Execute the command
    // Note: The positional arguments list includes 'viora' and the command name at index 0 and 1,
    // we strip these and pass the rest to the subcommand.
    QStringList cmdArgs = parser.positionalArguments();
    if (!cmdArgs.isEmpty() && cmdArgs.at(0) == command) {
        cmdArgs.removeFirst();
    } else if (cmdArgs.size() >= 2 && cmdArgs.at(1) == command) {
        cmdArgs.removeFirst();
        cmdArgs.removeFirst();
    } else {
        // Fallback or cleanup
        if (!cmdArgs.isEmpty()) {
            cmdArgs.removeFirst();
        }
    }

    int exitCode = cmd->execute(cmdArgs, parser);

    // Ensure buffers are flushed
    std::cout.flush();
    std::cerr.flush();

    // If the command is a persistent GUI view or live simulation run, the event loop runs.
    // Otherwise, we exit cleanly.
    if (command == "view" || command == "simulate") {
        if (exitCode == 0) {
            return app.exec();
        }
    }

    return exitCode;
}
