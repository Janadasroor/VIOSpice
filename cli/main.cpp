/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <iostream>
#include <string>

#include "command_registry.h"
#include "cli_daemon.h"

namespace {

// Commands that must stay in-process: they need a GUI (QApplication) or run an
// interactive/indefinite loop that cannot live inside the headless daemon.
bool isForwardable(const QString& command, const QStringList& args) {
    if (command == "view" || command == "simulate" ||
        command == "ext-watch" || command == "ext-preview") {
        return false;
    }
    // The flux REPL needs interactive stdin.
    if (command == "flux" && args.size() > 2 && args.at(2) == "repl") {
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    // Register command objects before the pre-scan loop so --help can
    // enumerate them (registration only constructs command objects and does
    // not require a QCoreApplication/QApplication or the QPA platform).
    registerAllCommands();

    // 1. Handle --help and --version before QApplication (avoids slow offscreen init)
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--help" || a == "-h") {
            CliDaemon::cliPrintGeneralHelp();
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

    if (!isView) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }


    // 3. Initialize Qt Application
    Q_UNUSED(isView)
    // Many CLI commands (erc, schematic-query, netlist-from-schematic, ...)
    // build QGraphicsScene/QGraphicsViews, which require a full QApplication
    // (QGraphicsScene's internals touch QGuiApplication/QApplication data).
    // Create QApplication unconditionally; the non-view path above already
    // selects the offscreen QPA platform to keep headless runs cheap.
    QScopedPointer<QApplication> app;
    app.reset(new QApplication(argc, argv));
    QCoreApplication::setApplicationName("viora");
    QCoreApplication::setApplicationVersion("1.0");

    // 4. Resolve subcommand
    QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        CliDaemon::cliPrintGeneralHelp();
        return 1;
    }

    QString command = args.at(1);
    if (command == "help" || command == "--help" || command == "-h") {
        if (args.size() > 2) {
            CliDaemon::cliPrintCommandHelp(args.at(2));
        } else {
            CliDaemon::cliPrintGeneralHelp();
        }
        return 0;
    }

    // Hidden entry point for the daemon's engine worker subprocess.
    if (command == "__worker") {
        return CliDaemon::workerMain();
    }

    if (command == "stop") {
        return CliDaemon::sendStop();
    }

    if (command == "serve") {
        int rc = CliDaemon::startServer();
        if (rc != 0) {
            return rc;
        }
        return app->exec();
    }

    auto* cmd = CommandRegistry::instance().getCommand(command);
    if (!cmd) {
        std::cerr << "Unknown command: " << command.toStdString() << "\n\n";
        CliDaemon::cliPrintGeneralHelp();
        return 1;
    }

    // 5. Parse & execute
    // --schema is static and needs no daemon or library init: run it locally.
    if (args.contains("--schema")) {
        return CliDaemon::cliRunCommand(app.data(), args, false);
    }

    // Otherwise try the persistent background daemon first so the heavy
    // one-time initialization is only paid once per machine/user.
    bool daemonEnabled = !qEnvironmentVariableIsSet("VIORA_NO_DAEMON");
    if (daemonEnabled && isForwardable(command, args)) {
        int fwdExit = CliDaemon::forwardCommand(args);
        if (fwdExit >= 0) {
            return fwdExit;
        }
        if (fwdExit == -2) {
            // A previous invocation of this command is still running on the
            // daemon. Do not spawn a competing daemon or re-run it in-process.
            std::cerr << "viora: the requested command is still running on the "
                         "background daemon (waiting for it to finish)\n";
            return 1;
        }
        int spawnedExit = -1;
        if (CliDaemon::spawnAndForward(args, &spawnedExit)) {
            return spawnedExit;
        }
        // Fall back to running in-process (daemon could not be started).
    }

    int exitCode = CliDaemon::cliRunCommand(app.data(), args, true);

    if (command == "view" || command == "simulate") {
        if (exitCode == 0) {
            return app->exec();
        }
    }

    return exitCode;
}