/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QJsonValue>
#include <QJsonObject>
#include <optional>
#include <iostream>
#include "simulator/core/sim_results.h"

extern bool g_quiet;
extern bool g_debug;
extern bool g_noColor;
extern bool g_exitOnWarning;

std::optional<int> parseTimeoutMs(const QString& value, QString* error = nullptr);
bool parseRangeOption(const QString& value, double* outStart, double* outEnd, QString* error = nullptr);
QJsonValue sortJsonValue(const QJsonValue& value);
void printJsonValue(const QJsonValue& value);
void printJsonValueTo(const QJsonValue& value, std::ostream& out);
// Set while the daemon's engine worker is running a command. Commands that
// print JSON then terminate the process with std::_Exit() would otherwise kill
// the worker before it can send its response frame back to the daemon.
extern bool g_daemonWorker;

// Thrown by commandExit() while inside the engine worker so the worker can
// write its response frame instead of being terminated mid-command.
struct CompletedRequest {
    int code;
};

// Exit the process with `code`. Under the daemon worker this instead throws
// CompletedRequest (caught by workerMain) so the worker survives to reply.
[[noreturn]] void commandExit(int code);

bool isWarningLine(const QString& msg);

QString stripAnsiCodes(const QString& text);
void printInfo(const QString& msg);
void printInfoStd(const std::string& msg);
QString analysisTypeToString(SimAnalysisType type);
QJsonObject resultsToJson(const SimResults& results);

class ScopedFdSilence {
public:
    explicit ScopedFdSilence(bool silence, bool stdErr = true) {}
    void release() {}
};
