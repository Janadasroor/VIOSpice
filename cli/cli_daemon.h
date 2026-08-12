/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

namespace CliDaemon {

// Name of the per-user local socket used for CLI <-> daemon communication.
QString socketName();

// Shared command runner used by both the foreground CLI and the daemon.
//   arguments     : full argv (index 0 = program name, index 1 = command)
//   initLibraries : run the heavy one-time initialization (registries + libraries)
// Returns the command's exit code. On success the process stdout/stderr are left as-is.
int cliRunCommand(QCoreApplication* app, const QStringList& arguments, bool initLibraries);

void cliPrintGeneralHelp();
void cliPrintCommandHelp(const QString& command);

// One-time heavy initialization (schematic/PCB registries, symbol + model libraries).
void cliInitializeLibraries();

// Client side -------------------------------------------------------------
// Attempt to run `arguments` on a running daemon. Returns the exit code if the
// request was served; returns -1 when no daemon is reachable.
int forwardCommand(const QStringList& arguments);

// Spawn a detached background daemon, wait for it to become ready (up to a
// generous timeout covering the one-time init), then run `arguments`.
// Returns true and sets *exitCode when the request was served.
bool spawnAndForward(const QStringList& arguments, int* exitCode);

// Server side -------------------------------------------------------------
// Start the persistent daemon: spawn the engine worker, listen, serve requests.
// Returns 0 on success (caller should then run the Qt event loop),
// or a non-zero code if the daemon could not be started.
int startServer();

// Hidden entry point for the per-user engine worker subprocess. The daemon
// spawns `viora __worker` so a crashing command cannot take the daemon down.
// Runs the one-time initialization, then serves requests from stdin.
int workerMain();

// Ask a running daemon to stop. Prints status and returns 0.
int sendStop();

} // namespace CliDaemon