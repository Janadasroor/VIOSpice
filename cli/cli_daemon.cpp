/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

// cli_daemon.cpp — Persistent background daemon for the viora CLI.
//
// The heavy one-time startup cost (schematic/PCB item registries, symbol
// libraries, model library reload) is paid exactly once by a detached daemon
// process. Every subsequent invocation connects over a per-user local socket,
// streams the request, and receives captured stdout/stderr plus the exit code —
// so repeated `viora <command>` calls are fast even when the underlying
// libraries take a minute to initialize.

#include "cli_daemon.h"
#include "command_registry.h"
#include "commands/common.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QLoggingCategory>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageLogContext>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QTimer>
#include <QThread>
#include <QDateTime>
#include <QProcess>
#include <QRegularExpression>

#include <iostream>
#include <sstream>
#include <string>

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

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace CliDaemon {

namespace {

// ---------------------------------------------------------------------------
// Output capture
// ---------------------------------------------------------------------------

// Qt logging (qWarning/qCritical/qInfo/qDebug) does not go through the
// std::cout/std::cerr buffers, so we sink it into a side buffer while a
// request runs. The daemon serves requests serially, so a single sink is safe.
static std::string g_qtErrorSink;

void captureMessageHandler(QtMsgType type, const QMessageLogContext& context,
                           const QString& msg) {
    Q_UNUSED(context)
    if (type == QtFatalMsg) {
        std::abort();
    }
    if (!msg.isEmpty()) {
        g_qtErrorSink += msg.toStdString();
        g_qtErrorSink += "\n";
    }
}

struct OutputCapture {
    std::stringbuf outBuf;
    std::stringbuf errBuf;
    std::streambuf* oldOut = nullptr;
    std::streambuf* oldErr = nullptr;
    QtMessageHandler oldHandler = nullptr;

    void begin() {
        g_qtErrorSink.clear();
        oldOut = std::cout.rdbuf(&outBuf);
        oldErr = std::cerr.rdbuf(&errBuf);
        oldHandler = qInstallMessageHandler(captureMessageHandler);
    }

    void end() {
        std::cout.flush();
        std::cerr.flush();
        std::cout.rdbuf(oldOut);
        std::cerr.rdbuf(oldErr);
        qInstallMessageHandler(oldHandler);
    }

    static QString captureToString(const std::stringbuf& buf) {
        std::string s = buf.str();
        return QString::fromUtf8(s.data(), int(s.size()));
    }

    QString out() const { return captureToString(outBuf); }
    QString err() const {
        QString qErr = captureToString(errBuf);
        if (!g_qtErrorSink.empty()) {
            QString qtErr = QString::fromUtf8(g_qtErrorSink.data(),
                                              int(g_qtErrorSink.size()));
            if (qErr.isEmpty()) {
                qErr = qtErr;
            } else if (!qErr.endsWith('\n')) {
                qErr += "\n";
                qErr += qtErr;
            } else {
                qErr += qtErr;
            }
        }
        return qErr;
    }
};

bool socketReachable(const QString& name) {
    QLocalSocket socket;
    socket.connectToServer(name);
    return socket.waitForConnected(1000);
}

} // namespace

// Bumped whenever the CLI <-> daemon protocol or socket layout changes, and
// baked into the socket name so a stale daemon from an older build can never
// satisfy a newer client (which would mis-forward requests).
constexpr int kDaemonProtocol = 1;

// ---------------------------------------------------------------------------
// Helpers (defined in CliDaemon scope, as declared in cli_daemon.h)
// ---------------------------------------------------------------------------

QString socketBase() {
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) {
        user = qEnvironmentVariable("USER");
    }
    if (user.isEmpty()) {
        user = QStringLiteral("default");
    }
    const QRegularExpression nonAlnum(QStringLiteral("[^A-Za-z0-9]"));
    user.replace(nonAlnum, QStringLiteral("_"));
    if (user.size() > 24) {
        user = user.left(24);
    }
    return QStringLiteral("viora-daemon-%1").arg(user);
}

QString socketName() {
    return QStringLiteral("%1-p%2").arg(socketBase()).arg(kDaemonProtocol);
}

// Per-user directory for daemon logs; created on demand.
QString daemonLogDir() {
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath();
    }
    const QString dir = base + QStringLiteral("/viora");
    QDir().mkpath(dir);
    return dir;
}

// Worker stderr lands here so crashes / Qt warnings inside the engine worker
// are diagnosed instead of vanishing into nullDevice().
QString workerLogPath() {
    return daemonLogDir() + QStringLiteral("/worker.log");
}

// Keep daemon logs bounded: move a log that grew past the cap aside, so the
// file the process keeps appending to cannot run away. No hard bound at the
// level of total history (old generations are expected to be purged by the OS
// user or installer).
constexpr qint64 kLogRotateBytes = 2 * 1024 * 1024;
void rotateLogIfLarge(const QString& path) {
    QFileInfo info(path);
    if (info.exists() && info.size() > kLogRotateBytes) {
        QFile::remove(path + QStringLiteral(".1"));
        QFile::rename(path, path + QStringLiteral(".1"));
    }
}

// Watchdog: a command running longer than this is killed and its client is
// answered with an error. Overridable via VIORA_CMD_TIMEOUT (seconds), read
// identically on both client and daemon so their clocks stay in agreement.
int commandTimeoutMs() {
    static const int ms = [] {
        int secs = qEnvironmentVariableIntValue("VIORA_CMD_TIMEOUT");
        if (secs <= 0) secs = 120;
        return secs * 1000;
    }();
    return ms;
}

// How long a client waits for a response on top of the watchdog window. The
// daemon-side watchdog only starts ticking once the command is dispatched to a
// live engine worker, but the client's clock starts the moment the request is
// written, so this extra budget must cover a full one-time worker (re)init
// that runs after a worker crash (or, on first connection, before the daemon
// even accepts sockets). Without it, a healthy daemon that is merely re-warming
// its engine after a crash would look "stalled" to the caller.
constexpr int kWorkerInitAllowanceMs = 120000;

// The daemon auto-shuts down after this long without any client activity.
// Overridable via VIORA_DAEMON_IDLE_TIMEOUT (seconds); 0 disables it.
int idleTimeoutMs() {
    static const int ms = [] {
        int secs = qEnvironmentVariableIntValue("VIORA_DAEMON_IDLE_TIMEOUT");
        return secs * 1000;
    }();
    return ms;
}

// Best effort: tell a daemon listening on `name` to stop.
bool stopDaemonNamed(const QString& name) {
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(2500)) {
        return false;
    }
    socket.write("{\"stop\":true}\n");
    socket.flush();
    socket.waitForBytesWritten(3000);
    return true;
}

void cliPrintGeneralHelp() {
    std::cout << "Usage: viora <command> [file] [options]\n\n";
    std::cout << "Available commands:\n";
    auto commands = CommandRegistry::instance().allCommands();
    for (auto* cmd : commands) {
        std::cout << "  " << cmd->name().leftJustified(22, ' ').toStdString()
                  << cmd->description().toStdString() << "\n";
    }
    std::cout << "\nTips:\n";
    std::cout << "  Use \"viora help <command>\" for command-specific help.\n";
    std::cout << "  Use --json for machine-readable output.\n";
}

void cliPrintCommandHelp(const QString& command) {
    auto* cmd = CommandRegistry::instance().getCommand(command);
    if (!cmd) {
        cliPrintGeneralHelp();
        return;
    }
    std::cout << cmd->name().toStdString() << " - "
              << cmd->description().toStdString() << "\n\n";

    QCommandLineParser parser;
    parser.setApplicationDescription(cmd->description());
    parser.addOption(QCommandLineOption("json",
        "Silence non-JSON output and format results as JSON"));
    parser.addOption(QCommandLineOption("quiet", "Silence non-JSON output"));
    parser.addOption(QCommandLineOption("debug", "Enable verbose debug output"));
    parser.addOption(QCommandLineOption("exit-on-warning",
        "Exit with non-zero code if warnings appear"));
    parser.addOption(QCommandLineOption("no-color", "Disable colored output"));
    parser.addOption(QCommandLineOption("schema",
        "Print JSON schema for the command and exit"));
    parser.addOption(QCommandLineOption(
        QStringList() << "h" << "help", "Show help for a command"));
    cmd->setupParser(parser);

    std::cout << parser.helpText().toStdString();
}

void cliInitializeLibraries() {
#if VIOSPICE_HAS_PCB
    PCBItemRegistry::registerBuiltInItems();
#endif
    SchematicItemRegistry::registerBuiltInItems();
    SymbolLibraryManager::instance().loadUserLibraries(
        QDir::homePath() + "/ViospiceLib/sym");
    ModelLibraryManager::instance().reload();
}

// ---------------------------------------------------------------------------
// Shared command runner (identical to the old main.cpp logic, but never calls
// parser.process()/showHelp(), which would exit the daemon process).
// ---------------------------------------------------------------------------

int cliRunCommand(QCoreApplication* app, const QStringList& arguments,
                  bool initLibraries) {
    Q_UNUSED(app)

    if (arguments.size() < 2) {
        cliPrintGeneralHelp();
        return 1;
    }

    const QString command = arguments.at(1);
    if (command == "help" || command == "--help" || command == "-h") {
        if (arguments.size() > 2) {
            cliPrintCommandHelp(arguments.at(2));
        } else {
            cliPrintGeneralHelp();
        }
        return 0;
    }

    auto* cmd = CommandRegistry::instance().getCommand(command);
    if (!cmd) {
        std::cerr << "Unknown command: " << command.toStdString() << "\n\n";
        cliPrintGeneralHelp();
        return 1;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(cmd->description());

    QCommandLineOption jsonOption("json",
        "Silence non-JSON output and format results as JSON");
    QCommandLineOption quietOption("quiet", "Silence non-JSON output");
    QCommandLineOption debugOption("debug", "Enable verbose debug output");
    QCommandLineOption exitWarnOption("exit-on-warning",
        "Exit with non-zero code if warnings appear");
    QCommandLineOption noColorOption("no-color", "Disable colored output");
    QCommandLineOption schemaOption("schema",
        "Print JSON schema for the command and exit");
    QCommandLineOption helpOption(
        QStringList() << "h" << "help", "Show help for a command");

    parser.addOption(jsonOption);
    parser.addOption(quietOption);
    parser.addOption(debugOption);
    parser.addOption(exitWarnOption);
    parser.addOption(noColorOption);
    parser.addOption(schemaOption);
    parser.addOption(helpOption);

    cmd->setupParser(parser);

    // Use parse() (not process()) so command-line errors cannot exit the
    // daemon process.
    if (!parser.parse(arguments)) {
        std::cerr << parser.errorText().toStdString() << "\n";
        return 1;
    }

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
        cliPrintCommandHelp(command);
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

    if (initLibraries) {
        cliInitializeLibraries();
    }

    // The positional arguments list includes 'viora' and the command name at
    // indexes 0 and 1; strip those and pass the rest to the subcommand.
    QStringList cmdArgs = parser.positionalArguments();
    if (!cmdArgs.isEmpty() && cmdArgs.at(0) == command) {
        cmdArgs.removeFirst();
    } else if (cmdArgs.size() >= 2 && cmdArgs.at(1) == command) {
        cmdArgs.removeFirst();
        cmdArgs.removeFirst();
    } else {
        if (!cmdArgs.isEmpty()) {
            cmdArgs.removeFirst();
        }
    }

    int exitCode = cmd->execute(cmdArgs, parser);

    std::cout.flush();
    std::cerr.flush();

    return exitCode;
}

// ---------------------------------------------------------------------------
// Client side
// ---------------------------------------------------------------------------

int forwardCommand(const QStringList& arguments) {
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(3000)) {
        return -1;
    }

    QJsonObject request;
    QJsonArray argvArr;
    for (const QString& a : arguments) {
        argvArr.append(a);
    }
    request["proto"] = kDaemonProtocol;
    request["argv"] = argvArr;

    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
    socket.flush();
    if (!socket.waitForBytesWritten(5000)) {
        return -1;
    }

    // Total deadline: a command that runs past this still gets its full
    // response closed (the daemon keeps serving everyone else), but the caller
    // does not hang forever. Returned as the -2 "stalled" sentinel.
    //
    // The daemon-side watchdog only starts its clock once the command has been
    // dispatched to a live engine worker, but this client-side clock starts the
    // moment the request is written. After a worker crash the daemon must
    // respawn the worker (a full one-time library init) before the command can
    // even begin, so the wait budget has to cover one worker (re)initialization
    // on top of the watchdog window; otherwise a healthy daemon that is simply
    // re-warming its engine looks like a stalled command.
    const qint64 deadline =
        QDateTime::currentMSecsSinceEpoch() + commandTimeoutMs() +
        kWorkerInitAllowanceMs;

    QByteArray response;
    while (!response.contains('\n')) {
        if (QDateTime::currentMSecsSinceEpoch() > deadline) {
            return -2; // command still running on the daemon; give up waiting
        }
        if (!socket.waitForReadyRead(10000)) {
            if (socket.state() != QLocalSocket::ConnectedState) {
                break; // daemon went away while running (or after reply)
            }
            if (socket.error() != QLocalSocket::UnknownSocketError) {
                break;
            }
            continue; // timed out, keep waiting
        }
        response += socket.readAll();
    }

    const int newline = response.indexOf('\n');
    if (newline < 0) {
        return -1;
    }
    response.truncate(newline);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return -1;
    }

    const QJsonObject obj = doc.object();
    const QJsonValue exitVal = obj.value("exit");
    if (!exitVal.isDouble()) {
        return -1;
    }
    const int exitCode = exitVal.toInt();

    const QByteArray out = obj.value("stdout").toString().toUtf8();
    const QByteArray err = obj.value("stderr").toString().toUtf8();
    if (!out.isEmpty()) {
        std::cout.write(out.constData(), out.size());
        std::cout.flush();
    }
    if (!err.isEmpty()) {
        std::cerr.write(err.constData(), err.size());
        std::cerr.flush();
    }
    return exitCode;
}

namespace {

bool spawnDaemon(const QString& program) {
#ifdef Q_OS_WIN
    QString path = program;
    QString arg = "\"" + path.replace("\"", "\\\"") + "\" serve";
    std::wstring cmdLine = arg.toStdWString();

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    return QProcess::startDetached(program, QStringList() << "serve");
#endif
}

} // namespace

bool spawnAndForward(const QStringList& arguments, int* exitCode) {
    const QString program = QCoreApplication::applicationFilePath();
    if (program.isEmpty() || !spawnDaemon(program)) {
        return false;
    }

    bool quiet = arguments.contains("--quiet") || arguments.contains("--json");
    if (!quiet) {
        std::cerr << "viora: starting background daemon (first call "
                     "initializes libraries; later calls are fast)\n"
                  << std::flush;
    }

    const qint64 deadline =
        QDateTime::currentMSecsSinceEpoch() + 150000; // covers one-time init
    while (true) {
        const int rc = forwardCommand(arguments);
        if (rc >= 0) {
            if (exitCode) {
                *exitCode = rc;
            }
            return true;
        }
        if (rc == -2) {
            // A command is stuck running on the daemon. It may eventually
            // finish, so do not spawn a competing daemon for it.
            return false;
        }
        if (QDateTime::currentMSecsSinceEpoch() > deadline) {
            break;
        }
        QThread::msleep(300);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Server side
// ---------------------------------------------------------------------------

namespace {

// ---------------------------------------------------------------------------
// Engine worker: a persistent subprocess that runs commands in isolation so a
// crashing command cannot take the daemon (and its cached initialization) down.
// Requests/responses are exchanged as newline-delimited JSON over stdio pipes.
// ---------------------------------------------------------------------------

// Partial output left between worker reads (the worker is read frame by frame).
QByteArray& workerOutBuffer() {
    static QByteArray buf;
    return buf;
}

QProcess*& workerProcess() {
    static QProcess* proc = nullptr;
    return proc;
}

// Reads one newline-terminated frame from the worker's stdout. Returns false
// if the worker died without producing a complete frame.
bool readWorkerFrame(QProcess* worker, QByteArray* line) {
    QByteArray& buf = workerOutBuffer();
    for (;;) {
        const int nl = buf.indexOf('\n');
        if (nl >= 0) {
            *line = buf.left(nl);
            buf = buf.mid(nl + 1);
            return true;
        }
        const QByteArray more = worker->readAllStandardOutput();
        if (!more.isEmpty()) {
            buf += more;
            continue;
        }
        if (worker->state() == QProcess::NotRunning) {
            return false; // worker crashed / exited without the frame
        }
        if (worker->waitForReadyRead(10000)) {
            continue;
        }
        // Ready-read timed out or the pipe closed. If the process has since
        // exited we treat it as a crash; otherwise the command is still
        // running and we keep waiting.
        if (worker->waitForFinished(1) ||
            worker->state() == QProcess::NotRunning) {
            return false;
        }
    }
}

void killWorker() {
    QProcess*& worker = workerProcess();
    if (worker) {
        if (worker->state() != QProcess::NotRunning) {
            worker->kill();
            worker->waitForFinished(3000);
        }
        delete worker;
        worker = nullptr;
    }
    workerOutBuffer().clear();
}

// ---------------------------------------------------------------------------
// Shared state + helpers for the asynchronous request handling below. These are
// declared here (ahead of ensureWorker, which wires the worker's signals) and
// the definitions live in the async section further down.
// ---------------------------------------------------------------------------

struct ClientSession;
QList<ClientSession*> g_sessions;
QList<ClientSession*> g_pending;
ClientSession* g_inFlight = nullptr;
bool g_workerReady = false;
QTimer* g_idleTimer = nullptr;

void sendResponse(ClientSession* cs, const QJsonObject& obj);
void dispatchNext();
void onWorkerData();
void onWorkerFinished(int code);
void spawnWorkerAsync();
void connectWorkerSignals(QProcess* worker);
void restartIdleTimer();
void startCommandWatchdog(ClientSession* cs);

// Spawns the worker (if needed) and blocks until it reports it is ready.
// Used once at daemon startup so socket reachability implies a warm worker.
// Returns true when a healthy worker is running.
bool ensureWorker() {
    QProcess*& worker = workerProcess();
    if (worker && worker->state() != QProcess::NotRunning) {
        return true;
    }
    killWorker();

    const QString program = QCoreApplication::applicationFilePath();
    if (program.isEmpty()) {
        return false;
    }

    worker = new QProcess;
    worker->setProgram(program);
    worker->setArguments({ QStringLiteral("__worker") });
    rotateLogIfLarge(workerLogPath());
    worker->setStandardErrorFile(workerLogPath(), QIODevice::Append);
#ifdef Q_OS_WIN
    worker->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* args) {
            args->flags |= 0x08000000; // CREATE_NO_WINDOW
        });
#endif
    worker->start();
    if (!worker->waitForStarted(10000)) {
        killWorker();
        return false;
    }

    QByteArray ready;
    if (!readWorkerFrame(worker, &ready) || !ready.contains("\"ready\"")) {
        // Worker died during the one-time initialization.
        killWorker();
        return false;
    }
    g_workerReady = true;
    connectWorkerSignals(worker);
    return true;
}

// ---------------------------------------------------------------------------
// Asynchronous request handling.
//
// The daemon never blocks its event loop while a command runs, so a slow (or
// stuck) command only holds the one client that asked for it while every other
// client keeps being served. Requests are queued and dispatched to the single
// worker in FIFO order; a worker crash answers the in-flight client with an
// error and respawns automatically. A command that outlives the watchdog (see
// commandTimeoutMs) is killed the same way, so a hung command cannot wedge the
// daemon forever.
// ---------------------------------------------------------------------------

struct ClientSession : QObject {
    QLocalSocket* socket = nullptr;
    QByteArray buf;
    QJsonObject request;
    bool done = false;
};

void finishSession(ClientSession* cs);
void sendResponse(ClientSession* cs, const QJsonObject& obj);
void handleWorkerTerminated(ClientSession* cs, int code, const QString& stderrMsg);

void connectWorkerSignals(QProcess* worker) {
    QObject::connect(worker, &QProcess::readyReadStandardOutput,
                     []() { onWorkerData(); });
    QObject::connect(
        worker, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [](int code, QProcess::ExitStatus) { onWorkerFinished(code); });
    QObject::connect(worker, &QProcess::errorOccurred,
                     [](QProcess::ProcessError err) {
                         if (err == QProcess::FailedToStart) {
                             onWorkerFinished(-1);
                         }
                     });
}

void finishSession(ClientSession* cs) {
    if (!cs || cs->done) return;
    cs->done = true;
    g_sessions.removeAll(cs);
    g_pending.removeAll(cs);
    if (g_inFlight == cs) g_inFlight = nullptr;
    if (cs->socket) {
        cs->socket->disconnectFromServer();
        cs->socket->deleteLater();
    }
    cs->deleteLater();
}

// Writes the reply and closes the session only once the socket has drained it.
// The daemon must not block its event loop (that would stall every other
// client), so for a reply that exceeds the socket buffer we keep the session
// open until QLocalSocket::bytesWritten reports everything flushed. The
// watchdog timer guarantees a slow-to-drain peer cannot hold a session forever.
void sendResponse(ClientSession* cs, const QJsonObject& obj) {
    if (!cs || cs->done) return;
    const QByteArray out =
        QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    if (cs->socket->state() != QLocalSocket::ConnectedState ||
        cs->socket->bytesToWrite() > 0) {
        finishSession(cs); // peer already gone or still draining old bytes
        return;
    }

    cs->socket->write(out);
    cs->socket->flush();
    if (cs->socket->bytesToWrite() == 0) {
        finishSession(cs);
        return;
    }

    QObject::connect(cs->socket, &QLocalSocket::bytesWritten, cs,
                     [cs]() {
                         if (!cs->done && cs->socket->bytesToWrite() == 0) {
                             finishSession(cs);
                         }
                     });
    QTimer* drain = new QTimer(cs);
    drain->setSingleShot(true);
    QObject::connect(drain, &QTimer::timeout, cs,
                     [cs]() { finishSession(cs); });
    drain->start(10000);
}

void onClientData(ClientSession* cs) {
    if (cs->done) return;
    restartIdleTimer();
    cs->buf += cs->socket->readAll();
    const int nl = cs->buf.indexOf('\n');
    if (nl < 0) return;

    QJsonParseError parseError;
    const QJsonDocument doc =
        QJsonDocument::fromJson(cs->buf.left(nl), &parseError);

    QJsonObject resp;
    resp["exit"] = 1;
    resp["stderr"] = "viora: malformed request";
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        cs->request = doc.object();
        if (cs->request.contains("stop")) {
            QJsonObject ok;
            ok["exit"] = 0;
            sendResponse(cs, ok);
            killWorker();
            QTimer::singleShot(0, QCoreApplication::instance(),
                               &QCoreApplication::quit);
            return;
        }
        if (cs->request.value("proto").toInt() != kDaemonProtocol) {
            QJsonObject resp;
            resp["exit"] = 2;
            resp["stderr"] =
                "viora: daemon protocol mismatch (stale client or daemon)";
            sendResponse(cs, resp);
            return;
        }
        g_pending.append(cs);
        dispatchNext();
        return;
    }
    sendResponse(cs, resp);
}

void onWorkerData() {
    QProcess*& worker = workerProcess();
    if (!worker) return;
    QByteArray& buf = workerOutBuffer();
    buf += worker->readAllStandardOutput();

    // Defensive backpressure cap: a command that floods stdout far beyond the
    // frame protocol would otherwise grow the buffer without bound. Treat it
    // like a crashed worker rather than risk exhausting memory.
    const qint64 kWorstFrame = 256 * 1024 * 1024;
    if (buf.size() > kWorstFrame) {
        ClientSession* cs = g_inFlight;
        g_inFlight = nullptr;
        if (cs) {
            QJsonObject resp;
            resp["exit"] = 1;
            resp["stderr"] =
                "viora: command flooded the worker pipe and was terminated";
            sendResponse(cs, resp);
        }
        handleWorkerTerminated(nullptr, -1, QStringLiteral("backpressure"));
        return;
    }

    int nl;
    while ((nl = buf.indexOf('\n')) >= 0) {
        const QByteArray line = buf.left(nl);
        buf = buf.mid(nl + 1);

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }
        const QJsonObject obj = doc.object();

        if (obj.contains("ready")) {
            g_workerReady = true;
            dispatchNext();
        } else if (obj.contains("exit")) {
            ClientSession* cs = g_inFlight;
            g_inFlight = nullptr;
            if (cs) sendResponse(cs, obj);
            restartIdleTimer();
            dispatchNext();
        }
    }
}

// Shared teardown for "the worker can no longer run the current command":
// answer the in-flight client, drop worker state, and respawn for whoever is
// queued. Responding to the client happens only when `cs` is actually the
// in-flight session; the worker cleanup always runs. A later QProcess
// finished signal sees an empty in-flight slot and becomes a no-op.
void handleWorkerTerminated(ClientSession* cs, int code,
                            const QString& stderrMsg) {
    if (cs && g_inFlight == cs) {
        g_inFlight = nullptr;
        QJsonObject resp;
        resp["exit"] = code ? code : 1;
        resp["stderr"] = stderrMsg;
        sendResponse(cs, resp);
    }
    g_workerReady = false;
    QProcess*& worker = workerProcess();
    if (worker) {
        if (worker->state() != QProcess::NotRunning) {
            worker->kill();
            worker->waitForFinished(2000);
        }
        delete worker;
        worker = nullptr;
    }
    workerOutBuffer().clear();
    restartIdleTimer(); // command (or its worker) is gone; not busy anymore
    dispatchNext();     // respawn a fresh worker for queued requests
}

void onWorkerFinished(int code) {
    ClientSession* cs = g_inFlight;
    handleWorkerTerminated(
        cs, code ? code : 1,
        QString("viora: command crashed inside the engine worker (exit "
                "code %1)")
            .arg(code));
}

// Arms the watchdog for the command about to run: an unresponsive command that
// outlives commandTimeoutMs is killed exactly like a crash, so one stuck
// command cannot wedge the daemon for everyone else.
void startCommandWatchdog(ClientSession* cs) {
    if (commandTimeoutMs() <= 0) return;
    QTimer* timer = new QTimer(cs);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, cs, [cs]() {
        if (g_inFlight != cs) return;
        handleWorkerTerminated(
            cs, 124,
            QString("viora: command timed out after %1s and was terminated")
                .arg(commandTimeoutMs() / 1000));
    });
    timer->start(commandTimeoutMs());
}

void restartIdleTimer() {
    if (!g_idleTimer) return;
    const int ms = idleTimeoutMs();
    if (ms > 0) g_idleTimer->start(ms);
}

// A command currently running for the daemon is not "idle": pause the idle
// timer while work is dispatched and resume it once the command completes.
void pauseIdleTimer() {
    if (g_idleTimer && g_idleTimer->isActive()) {
        g_idleTimer->stop();
    }
}

void spawnWorkerAsync() {
    QProcess*& worker = workerProcess();
    if (worker && worker->state() != QProcess::NotRunning) {
        return;
    }

    const QString program = QCoreApplication::applicationFilePath();
    if (program.isEmpty()) {
        // No way to run commands; fail every queued request.
        while (!g_pending.isEmpty()) {
            ClientSession* p = g_pending.takeFirst();
            QJsonObject resp;
            resp["exit"] = 1;
            resp["stderr"] = "viora: cannot start engine worker";
            sendResponse(p, resp);
        }
        return;
    }

    worker = new QProcess;
    worker->setProgram(program);
    worker->setArguments({ QStringLiteral("__worker") });
    rotateLogIfLarge(workerLogPath());
    worker->setStandardErrorFile(workerLogPath(), QIODevice::Append);
#ifdef Q_OS_WIN
    worker->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* args) {
            args->flags |= 0x08000000; // CREATE_NO_WINDOW
        });
#endif
    g_workerReady = false;
    workerOutBuffer().clear();
    connectWorkerSignals(worker);
    worker->start();
    if (!worker->waitForStarted(10000)) {
        onWorkerFinished(-1);
    }
}

void dispatchNext() {
    while (!g_pending.isEmpty() && !g_inFlight) {
        QProcess*& worker = workerProcess();
        if (!worker || worker->state() == QProcess::NotRunning) {
            spawnWorkerAsync();
            return; // dispatch resumes once the worker reports ready
        }
        if (!g_workerReady) {
            return;
        }

        ClientSession* cs = g_pending.takeFirst();
        g_inFlight = cs;
        pauseIdleTimer();

        QJsonObject req;
        req["proto"] = kDaemonProtocol;
        req["argv"] = cs->request.value("argv");
        worker->write(QJsonDocument(req).toJson(QJsonDocument::Compact) +
                      "\n");
        startCommandWatchdog(cs);
    }
}

void onNewConnection(QLocalServer* server) {
    restartIdleTimer();
    while (QLocalSocket* socket = server->nextPendingConnection()) {
        auto* cs = new ClientSession;
        cs->socket = socket;
        socket->setParent(cs);
        g_sessions.append(cs);

        QObject::connect(socket, &QLocalSocket::readyRead, cs,
                         [cs]() { onClientData(cs); });
        QObject::connect(socket, &QLocalSocket::disconnected, cs,
                         [cs]() { finishSession(cs); });

        // Drop clients that connect but never send a request.
        QTimer* timer = new QTimer(cs);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, cs,
                         [cs]() { finishSession(cs); });
        timer->start(60000);
    }
}

} // namespace

int startServer() {
    static QLocalServer server;

    // Never stomp on a live daemon: fail fast when one is already reachable
    // (also skips the heavy init below in that case).
    if (socketReachable(socketName())) {
        return 1;
    }

    // Stop any daemon left behind by an older build. Versioned socket names
    // make it unreachable to new clients, so shut it down explicitly rather
    // than leave an orphan holding the warmed worker's memory. Also sweep the
    // pre-versioning name (protocol had no suffix) for the first bump.
    for (int p = 0; p < kDaemonProtocol; ++p) {
        const QString name =
            (p == 0) ? socketBase()
                     : QStringLiteral("%1-p%2").arg(socketBase()).arg(p);
        stopDaemonNamed(name);
    }

    // Sockets only become reachable once the engine worker has finished its
    // one-time initialization, which is what the spawn/forward client waits
    // for. A command that later crashes only kills the worker, not the daemon.
    if (!ensureWorker()) {
        std::cerr << "viora: failed to start engine worker for socket "
                  << socketName().toStdString() << "\n";
        return 1;
    }

    // No daemon is currently listening; clear a socket left by a crashed
    // daemon (if any) before taking over the name.
    server.removeServer(socketName());
    // Restrict the named pipe to the owning user only. On Windows the default
    // DACL lets any local account connect; without this, a colleague (or
    // malware) could execute arbitrary viora commands as this user.
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(socketName())) {
        if (server.serverError() == QAbstractSocket::AddressInUseError &&
            socketReachable(socketName())) {
            // A real daemon is already running; nothing to do for us.
            return 1;
        }
        std::cerr << "viora: daemon failed to listen on "
                  << socketName().toStdString() << ": "
                  << server.errorString().toStdString() << "\n";
        return 1;
    }

    QLocalServer* serverPtr = &server;
    QObject::connect(&server, &QLocalServer::newConnection,
                     [serverPtr]() { onNewConnection(serverPtr); });

    // Auto-shutdown after a quiet period so an unused daemon does not hold the
    // warmed worker's memory forever.
    if (idleTimeoutMs() > 0) {
        g_idleTimer = new QTimer(QCoreApplication::instance());
        g_idleTimer->setSingleShot(true);
        QObject::connect(
            g_idleTimer, &QTimer::timeout, g_idleTimer,
            []() {
                killWorker();
                QCoreApplication::instance()->quit();
            });
        restartIdleTimer();
    }
    return 0;
}

int workerMain() {
    // One-time initialization, exactly like a cold in-process CLI run. The
    // daemon waits for the READY frame below before serving any request, so a
    // command crashing later cannot take this initialization down with it.
    cliInitializeLibraries();

    std::cout << "{\"ready\":true}\n" << std::flush;

    std::string line;
    while (std::getline(std::cin, line)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(
            QByteArray::fromStdString(line), &parseError);

        QJsonObject response;
        response["exit"] = 1;
        response["stderr"] = "viora: malformed worker request";

        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            QStringList argv;
            const QJsonArray argvArr = doc.object().value("argv").toArray();
            for (const QJsonValue& v : argvArr) {
                argv << v.toString();
            }

            OutputCapture capture;
            capture.begin();
            const int rc = qApp ? cliRunCommand(qApp, argv, false) : 1;
            capture.end();

            response["exit"] = rc;
            response["stdout"] = capture.out();
            response["stderr"] = capture.err();
        }

        std::cout << QJsonDocument(response).toJson(QJsonDocument::Compact)
                         .constData()
                  << "\n"
                  << std::flush;
    }

    return 0;
}

int sendStop() {
    if (!stopDaemonNamed(socketName())) {
        std::cerr << "viora: no daemon is running\n";
        return 1;
    }
    std::cerr << "viora: daemon stopped\n";
    return 0;
}

} // namespace CliDaemon