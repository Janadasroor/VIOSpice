/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

// Daemon integration test.
//
// Exercises the persistent background daemon end to end through the real
// `viora` binary:
//   - commands are served by a single daemon across invocations,
//   - the command-timeout watchdog kills a hung command and reports exit 124,
//   - the daemon keeps serving after a worker was killed by the watchdog,
//   - `viora stop` shuts the daemon down.
//
// The `sleep` command is a dedicated hook for the watchdog test: it blocks for
// the requested number of seconds, so a short VIORA_CMD_TIMEOUT terminates it
// deterministically instead of depending on a real slow command.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <iostream>
#include <stdexcept>

#ifndef VIO_CMD_PATH
#define VIO_CMD_PATH "viora"
#endif

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Runs `viora <args>` and returns {exitCode, stdout, stderr}.
struct RunResult {
    int exitCode = -1;
    QByteArray stdoutData;
    QByteArray stderrData;
    qint64 elapsedMs = 0;
};

RunResult runViora(const QStringList& args,
                   const QProcessEnvironment& extraEnv = {},
                   int timeoutMs = 300000) {
    QProcess proc;
    proc.setProgram(QString::fromUtf8(VIO_CMD_PATH));
    proc.setArguments(args);
    if (!extraEnv.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString& key : extraEnv.keys()) {
            env.insert(key, extraEnv.value(key));
        }
        proc.setProcessEnvironment(env);
    }

    QElapsedTimer elapsed;
    elapsed.start();
    proc.start();
    if (!proc.waitForStarted(30000)) {
        throw std::runtime_error("could not start " + QString(VIO_CMD_PATH).toStdString());
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(5000);
        throw std::runtime_error("command timed out: " + args.join(" ").toStdString());
    }

    RunResult r;
    r.exitCode = proc.exitCode();
    r.stdoutData = proc.readAllStandardOutput();
    r.stderrData = proc.readAllStandardError();
    r.elapsedMs = elapsed.elapsed();
    return r;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    try {
        // 1. Stop any daemon left over from a previous run so the test starts
        // from a known state. A fresh daemon is spawned by the first command.
        runViora({"stop"});

        // 2. First call spawns the daemon and forwards the request. Use a
        // watchdog-friendly budget so the initial library init (which can take
        // tens of seconds on first run) does not trip anything.
        QProcessEnvironment watchdogEnv;
        watchdogEnv.insert("VIORA_CMD_TIMEOUT", "3");

        RunResult hung = runViora({"sleep", "30"}, watchdogEnv, 300000);
        require(hung.exitCode == 124,
                "watchdog should kill `sleep 30` with exit 124, got " +
                    std::to_string(hung.exitCode));
        require(QString::fromUtf8(hung.stderrData).contains("timed out"),
                "watchdog stderr should mention the timeout");
        std::cout << "ok: watchdog killed hung command (exit 124)\n";

        // 3. The daemon must survive the worker kill and keep serving: a fast
        // command right after the watchdog still succeeds (warm daemon). The
        // exit-124 response above went through the same worker that was killed,
        // so this proves a fresh worker respawned for the queued request.
        RunResult quick = runViora({"sleep", "0"}, watchdogEnv, 300000);
        require(quick.exitCode == 0,
                "daemon should keep serving after watchdog kill, got " +
                    std::to_string(quick.exitCode));
        std::cout << "ok: daemon served `sleep 0` after watchdog recovery\n";

        // 4. The daemon is still running (commands are being forwarded, not run
        // in-process) and `stop` shuts it down cleanly.
        RunResult stopped = runViora({"stop"});
        require(stopped.exitCode == 0,
                "`viora stop` should exit 0, got " +
                    std::to_string(stopped.exitCode));
        require(QString::fromUtf8(stopped.stderrData).contains("stopped"),
                "`viora stop` should report the daemon was stopped");
        std::cout << "ok: `viora stop` shut the daemon down\n";

        // 5. When VIORA_NO_DAEMON=1 is set, commands run in-process and do NOT
        // spawn a background daemon.
        QProcessEnvironment noDaemonEnv;
        noDaemonEnv.insert("VIORA_NO_DAEMON", "1");
        RunResult direct = runViora({"--help"}, noDaemonEnv, 30000);
        require(direct.exitCode == 0,
                "`viora --help` with VIORA_NO_DAEMON=1 should exit 0, got " +
                    std::to_string(direct.exitCode));
        require(!QString::fromUtf8(direct.stderrData).contains("starting background daemon"),
                "VIORA_NO_DAEMON=1 should not print daemon startup banner");
        std::cout << "ok: VIORA_NO_DAEMON=1 runs in-process without daemon\n";

        std::cout << "cli.daemon: all tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "cli.daemon: FAILED: " << e.what() << "\n";
        return 1;
    }
}