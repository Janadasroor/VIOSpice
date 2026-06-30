/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"
#include "simulator/core/sim_value_parser.h"
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <limits>
#include <cmath>

bool g_quiet = false;
bool g_debug = false;
bool g_noColor = false;
bool g_exitOnWarning = false;

std::optional<int> parseTimeoutMs(const QString& value, QString* error) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = "Timeout is empty.";
        return std::nullopt;
    }

    const QRegularExpression re(R"(^\s*([0-9]*\.?[0-9]+)\s*(ms|s|m)?\s*$)");
    const QRegularExpressionMatch match = re.match(trimmed);
    if (!match.hasMatch()) {
        if (error) *error = "Invalid timeout format. Use values like 10s or 5000ms.";
        return std::nullopt;
    }

    bool ok = false;
    const double number = match.captured(1).toDouble(&ok);
    if (!ok || number < 0) {
        if (error) *error = "Timeout must be a non-negative number.";
        return std::nullopt;
    }

    const QString unit = match.captured(2);
    double ms = number;
    if (unit == "ms" || unit.isEmpty()) {
        ms = number;
    } else if (unit == "s") {
        ms = number * 1000.0;
    } else if (unit == "m") {
        ms = number * 60000.0;
    }

    if (ms > static_cast<double>(std::numeric_limits<int>::max())) {
        if (error) *error = "Timeout is too large.";
        return std::nullopt;
    }

    return static_cast<int>(ms);
}

bool parseRangeOption(const QString& value, double* outStart, double* outEnd, QString* error) {
    if (outStart) *outStart = std::numeric_limits<double>::quiet_NaN();
    if (outEnd) *outEnd = std::numeric_limits<double>::quiet_NaN();
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) return true;
    const QStringList parts = trimmed.split(':');
    if (parts.size() != 2) {
        if (error) *error = "Invalid range format. Use t0:t1 (e.g. 1ms:5ms).";
        return false;
    }
    double t0 = 0.0;
    double t1 = 0.0;
    if (!SimValueParser::parseSpiceNumber(parts[0].trimmed(), t0) ||
        !SimValueParser::parseSpiceNumber(parts[1].trimmed(), t1)) {
        if (error) *error = "Invalid range values. Use spice numbers like 1ms:5ms.";
        return false;
    }
    if (outStart) *outStart = t0;
    if (outEnd) *outEnd = t1;
    return true;
}

QJsonValue sortJsonValue(const QJsonValue& value) {
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        QStringList keys = obj.keys();
        keys.sort(Qt::CaseInsensitive);
        QJsonObject sorted;
        for (const auto& key : keys) {
            sorted.insert(key, sortJsonValue(obj.value(key)));
        }
        return sorted;
    }
    if (value.isArray()) {
        QJsonArray arr;
        const QJsonArray in = value.toArray();
        for (const auto& v : in) arr.append(sortJsonValue(v));
        return arr;
    }
    return value;
}

void printJsonValue(const QJsonValue& value) {
    const QJsonValue sorted = sortJsonValue(value);
    QJsonDocument doc = sorted.isArray() ? QJsonDocument(sorted.toArray()) : QJsonDocument(sorted.toObject());
    std::cout << doc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
}

void printJsonValueTo(const QJsonValue& value, std::ostream& out) {
    const QJsonValue sorted = sortJsonValue(value);
    QJsonDocument doc = sorted.isArray() ? QJsonDocument(sorted.toArray()) : QJsonDocument(sorted.toObject());
    out << doc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
}

bool isWarningLine(const QString& msg) {
    const QString trimmed = msg.trimmed();
    if (trimmed.isEmpty()) return false;
    const QString lower = trimmed.toLower();
    return lower.startsWith("warning") || lower.contains(" warning") || lower.contains("warning:");
}

QString stripAnsiCodes(const QString& text) {
    if (!g_noColor) return text;
    static const QRegularExpression ansiRe("\x1B\\[[0-9;?]*[ -/]*[@-~]");
    QString out = text;
    out.remove(ansiRe);
    return out;
}

void printInfo(const QString& msg) {
    if (!g_quiet) std::cerr << stripAnsiCodes(msg).toStdString() << std::endl;
}

void printInfoStd(const std::string& msg) {
    if (!g_quiet) std::cerr << stripAnsiCodes(QString::fromStdString(msg)).toStdString() << std::endl;
}

QString analysisTypeToString(SimAnalysisType type) {
    switch (type) {
        case SimAnalysisType::OP: return "op";
        case SimAnalysisType::Transient: return "transient";
        case SimAnalysisType::AC: return "ac";
        case SimAnalysisType::MonteCarlo: return "monte_carlo";
        case SimAnalysisType::Sensitivity: return "sensitivity";
        case SimAnalysisType::ParametricSweep: return "parametric_sweep";
        case SimAnalysisType::Noise: return "noise";
        case SimAnalysisType::Distortion: return "distortion";
        case SimAnalysisType::Optimization: return "optimization";
        case SimAnalysisType::FFT: return "fft";
        case SimAnalysisType::RealTime: return "real_time";
    }
    return "unknown";
}

QJsonObject resultsToJson(const SimResults& results) {
    QJsonObject root;
    root["analysis"] = analysisTypeToString(results.analysisType);
    root["xAxis"] = QString::fromStdString(results.xAxisName);
    root["yAxis"] = QString::fromStdString(results.yAxisName);

    QJsonArray waves;
    for (const auto& wave : results.waveforms) {
        QJsonObject w;
        w["name"] = QString::fromStdString(wave.name);
        
        QJsonArray xData;
        for (double val : wave.xData) xData.append(val);
        w["x"] = xData;

        QJsonArray yData;
        for (double val : wave.yData) yData.append(val);
        w["y"] = yData;

        if (!wave.yPhase.empty()) {
            QJsonArray yPhase;
            for (double val : wave.yPhase) yPhase.append(val);
            w["phase"] = yPhase;
        }
        waves.append(w);
    }
    root["waveforms"] = waves;

    QJsonObject nodes;
    for (auto it = results.nodeVoltages.begin(); it != results.nodeVoltages.end(); ++it) {
        nodes[QString::fromStdString(it->first)] = it->second;
    }
    root["nodeVoltages"] = nodes;

    QJsonObject branches;
    for (auto it = results.branchCurrents.begin(); it != results.branchCurrents.end(); ++it) {
        branches[QString::fromStdString(it->first)] = it->second;
    }
    root["branchCurrents"] = branches;

    QJsonObject measurements;
    for (auto it = results.measurements.begin(); it != results.measurements.end(); ++it) {
        measurements[QString::fromStdString(it->first)] = it->second;
    }
    root["measurements"] = measurements;

    QJsonObject measurementMetadata;
    for (auto it = results.measurementMetadata.begin(); it != results.measurementMetadata.end(); ++it) {
        QJsonObject meta;
        meta["quantityLabel"] = QString::fromStdString(it->second.quantityLabel);
        meta["displayUnit"] = QString::fromStdString(it->second.displayUnit);
        measurementMetadata[QString::fromStdString(it->first)] = meta;
    }
    root["measurementMetadata"] = measurementMetadata;

    QJsonArray diags;
    for (const auto& d : results.diagnostics) diags.append(QString::fromStdString(d));
    root["diagnostics"] = diags;

    return root;
}
