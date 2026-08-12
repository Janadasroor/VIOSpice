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

// ---------------------------------------------------------------------------
// Helpers (defined in CliDaemon scope, as declared in cli_daemon.h)
// ---------------------------------------------------------------------------

QString socketName() {
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
    request["argv"] = argvArr;

    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
    socket.flush();
    if (!socket.waitForBytesWritten(5000)) {
        return -1;
    }

    QByteArray response;
    while (!response.contains('\n')) {
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

// Spawns the worker (if needed) and waits until it reports it is ready.
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
    worker->setStandardErrorFile(QProcess::nullDevice());
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
    return true;
}

void serveOneClient(QLocalSocket* socket) {
    QByteArray data;
    while (!data.contains('\n')) {
        if (!socket->waitForReadyRead(30000)) {
            if (socket->state() != QLocalSocket::ConnectedState) {
                break;
            }
            if (socket->error() != QLocalSocket::UnknownSocketError) {
                break;
            }
            continue;
        }
        data += socket->readAll();
    }

    const int newline = data.indexOf('\n');
    QByteArray line = (newline < 0) ? data : data.left(newline);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);

    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject request = doc.object();

        if (request.contains("stop")) {
            socket->write("{\"exit\":0}\n");
            socket->flush();
            socket->waitForBytesWritten(2000);
            socket->disconnectFromServer();
            socket->deleteLater();
            killWorker();
            QTimer::singleShot(0, QCoreApplication::instance(),
                               &QCoreApplication::quit);
            return;
        }

        QJsonObject response;
        response["exit"] = 1;
        response["stderr"] = "viora: daemon failed to start engine worker";

        if (ensureWorker()) {
            QProcess* worker = workerProcess();

            QJsonObject req;
            req["argv"] = request.value("argv");
            worker->write(QJsonDocument(req).toJson(QJsonDocument::Compact) +
                          "\n");

            if (worker->waitForBytesWritten(5000)) {
                QByteArray respLine;
                if (readWorkerFrame(worker, &respLine)) {
                    const QJsonDocument respDoc =
                        QJsonDocument::fromJson(respLine, &parseError);
                    if (parseError.error == QJsonParseError::NoError &&
                        respDoc.isObject()) {
                        response = respDoc.object();
                    } else {
                        response["exit"] = 1;
                        response["stderr"] =
                            "viora: invalid response from engine worker";
                    }
                } else {
                    // Worker crashed while running the command: report the
                    // crash code; a fresh worker is spawned on next request.
                    worker->waitForFinished(2000);
                    const int crashCode = worker->exitCode();
                    response["exit"] = crashCode ? crashCode : 1;
                    response["stderr"] = QString(
                        "viora: command crashed inside the engine worker "
                        "(exit code %1)").arg(crashCode);
                    killWorker();
                }
            } else {
                killWorker();
            }
        }

        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) +
                      "\n");
        socket->flush();
        socket->waitForBytesWritten(5000);
    } else {
        socket->write("{\"exit\":1}\n");
        socket->flush();
    }

    socket->disconnectFromServer();
    socket->deleteLater();
}

void onNewConnection(QLocalServer* server) {
    while (QLocalSocket* socket = server->nextPendingConnection()) {
        serveOneClient(socket);
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
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(3000)) {
        std::cerr << "viora: no daemon is running\n";
        return 1;
    }

    socket.write("{\"stop\":true}\n");
    socket.flush();
    socket.waitForBytesWritten(5000);

    QByteArray response;
    while (!response.contains('\n')) {
        if (!socket.waitForReadyRead(5000)) {
            break;
        }
        response += socket.readAll();
    }

    std::cerr << "viora: daemon stopped\n";
    return 0;
}

} // namespace CliDaemon