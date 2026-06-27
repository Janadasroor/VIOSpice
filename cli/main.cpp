/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "schematic_item.h"
#include "factories/schematic_item_registry.h"
#include "factories/schematic_item_factory.h"
#include "items/virtual_terminal_item.h"
#include "items/instrument_probe_item.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QGraphicsScene>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QCryptographicHash>
#include <QPainterPath>
#include <QDirIterator>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include "../simulator/core/sim_report_generator.h"
#include "../simulator/core/raw_data_parser.h"
#include "../utils/schematic_url_encoder.h"
#include <QLoggingCategory>
#include <QProcess>
#include <QTcpSocket>
#include <QVariantMap>
#include <QRandomGenerator>
#include "python/cpp/core/flux_script_manager.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <optional>
#include <memory>
// --- Cross-platform signal handling ---
// We use QCoreApplication::aboutToQuit or QTimer to ensure clean termination
// instead of platform-specific signal handlers.

#include "core/flux/extensions/native/plugin_manager.h"
// Symbols
#include "symbols/models/symbol_definition.h"
#include "symbols/symbol_library.h"
#include "symbols/kicad_symbol_importer.h"
#include "symbols/ltspice_symbol_importer.h"
// PCB Includes (optional)
#if __has_include("pcb/drc/pcb_drc.h")
#define VIOSPICE_HAS_PCB 1
#include "vioraeda/drc/pcb_drc.h"
#include "vioraeda/factories/pcb_item_registry.h"
#include "vioraeda/io/pcb_file_io.h"
#else
#define VIOSPICE_HAS_PCB 0
#endif

// Schematic Includes
#include "flux/schematic/analysis/schematic_annotator.h"
#include "flux/schematic/analysis/schematic_erc.h"
#include "schematic/analysis/spice_netlist_generator.h"
#include "schematic/items/schematic_item.h"
#include "flux/schematic/factories/schematic_item_registry.h"
#include "flux/schematic/io/schematic_file_io.h"
#include "schematic/io/netlist_generator.h"
#include "schematic/io/netlist_to_schematic.h"
#include "flux/schematic/items/wire_item.h"
#include "flux/schematic/editor/schematic_api.h"
#if VIOSPICE_HAS_PCB
#include "vioraeda/editor/pcb_api.h"
#include "vioraeda/io/pcb_file_io.h"
#endif

#include "schematic/analysis/spice_netlist_generator.h"
#include "simulator/bridge/sim_schematic_bridge.h"
#include "simulation_manager.h"
#include "simulator/bridge/sim_manager.h"
#include "simulator/core/sim_results.h"
#include "simulator/core/sim_value_parser.h"
#include "simulator/bridge/model_library_manager.h"
#include "simulator/core/raw_data_parser.h"
#include <QMainWindow>
#include "../ui/waveform_viewer.h"
#include "../schematic/ui/simulation_panel.h"
#include "simulator/bridge/slang_manager.h"

// FluxScript Integration
#include "flux_command.h"

// Extension command handlers (defined in extension_command.cpp)
int cmdExtensionInit(const QStringList& args);
int cmdExtensionValidate(const QStringList& args);
int cmdExtensionInstall(const QStringList& args);

namespace {
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

static bool parseRangeOption(const QString& value, double* outStart, double* outEnd, QString* error) {
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

static QJsonValue sortJsonValue(const QJsonValue& value) {
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

static void printJsonValue(const QJsonValue& value) {
    const QJsonValue sorted = sortJsonValue(value);
    QJsonDocument doc = sorted.isArray() ? QJsonDocument(sorted.toArray()) : QJsonDocument(sorted.toObject());
    std::cout << doc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
}

static void printJsonValueTo(const QJsonValue& value, std::ostream& out) {
    const QJsonValue sorted = sortJsonValue(value);
    QJsonDocument doc = sorted.isArray() ? QJsonDocument(sorted.toArray()) : QJsonDocument(sorted.toObject());
    out << doc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
}

static bool isWarningLine(const QString& msg) {
    const QString trimmed = msg.trimmed();
    if (trimmed.isEmpty()) return false;
    const QString lower = trimmed.toLower();
    return lower.startsWith("warning") || lower.contains(" warning") || lower.contains("warning:");
}

class ScopedFdSilence {
public:
    explicit ScopedFdSilence(bool, bool = true) {}
    void release() {}
};


static bool resolveBaseSignalIndex(const RawData& data, const QString& name, int* outIndex, QString* error) {
    if (outIndex) *outIndex = -1;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return true;
    int idx = -1;
    for (int i = 0; i < (int)data.varNames.size(); ++i) {
        if (QString::fromStdString(data.varNames[i]).compare(trimmed, Qt::CaseInsensitive) == 0) { idx = i; break; }
    }
    if (idx < 1) {
        if (error) *error = QString("Base signal not found (or invalid): %1").arg(trimmed);
        return false;
    }
    if (outIndex) *outIndex = idx - 1;
    return true;
}

static QVector<int> decimatedIndices(const RawData& data, int baseSignalIndex, int maxPoints, double tStart, double tEnd) {
    const int total = data.x.size();
    QVector<int> out;
    if (total <= 0) return out;
    const bool useRange = !(std::isnan(tStart) || std::isnan(tEnd));
    const double rangeStart = useRange ? qMin(tStart, tEnd) : 0.0;
    const double rangeEnd = useRange ? qMax(tStart, tEnd) : 0.0;

    QVector<int> candidates;
    candidates.reserve(total);
    for (int i = 0; i < total; ++i) {
        if (!useRange || (data.x[i] >= rangeStart && data.x[i] <= rangeEnd)) candidates.push_back(i);
    }
    if (candidates.isEmpty()) return out;

    if (maxPoints <= 0 || candidates.size() <= maxPoints) {
        return candidates;
    }

    const int buckets = qMax(1, maxPoints / 2);
    const int bucketSize = qMax(1, (candidates.size() + buckets - 1) / buckets);
    out.reserve(qMin(maxPoints, total));
    const auto& base = data.y[baseSignalIndex];

    for (int start = 0; start < candidates.size(); start += bucketSize) {
        const int end = qMin(candidates.size(), start + bucketSize);
        int minIdx = candidates[start];
        int maxIdx = candidates[start];
        double minVal = base[minIdx];
        double maxVal = base[maxIdx];
        for (int i = start + 1; i < end; ++i) {
            const int idx = candidates[i];
            const double v = base[idx];
            if (v < minVal) { minVal = v; minIdx = idx; }
            if (v > maxVal) { maxVal = v; maxIdx = idx; }
        }
        if (minVal == maxVal) {
            if (out.isEmpty() || out.back() != minIdx) out.push_back(minIdx);
        } else if (minIdx < maxIdx) {
            if (out.isEmpty() || out.back() != minIdx) out.push_back(minIdx);
            if (out.back() != maxIdx) out.push_back(maxIdx);
        } else {
            if (out.isEmpty() || out.back() != maxIdx) out.push_back(maxIdx);
            if (out.back() != minIdx) out.push_back(minIdx);
        }
        if (out.size() >= maxPoints) break;
    }

    while (out.size() > maxPoints) out.pop_back();
    return out;
}

static QVector<int> filteredIndices(const RawData& data, double tStart, double tEnd) {
    QVector<int> out;
    const bool useRange = !(std::isnan(tStart) || std::isnan(tEnd));
    const double rangeStart = useRange ? qMin(tStart, tEnd) : 0.0;
    const double rangeEnd = useRange ? qMax(tStart, tEnd) : 0.0;
    out.reserve(data.x.size());
    for (int i = 0; i < data.x.size(); ++i) {
        if (!useRange || (data.x[i] >= rangeStart && data.x[i] <= rangeEnd)) out.push_back(i);
    }
    return out;
}

static QJsonObject rawToJson(const RawData& data, const QStringList& signalNames, const QVector<int>& indices, int maxPoints, double tStart, double tEnd, int baseSignalIndex = -1) {
    QJsonObject out;
    QJsonArray xArr;
    int baseSignal = baseSignalIndex;
    if (baseSignal < 0 || baseSignal >= data.y.size()) {
        baseSignal = indices.isEmpty() ? 0 : indices[0];
    }
    const QVector<int> idx = decimatedIndices(data, baseSignal, maxPoints, tStart, tEnd);
    for (int i : idx) xArr.append(data.x[i]);
    out["x"] = xArr;
    QJsonArray sigArr;
    for (int i = 0; i < signalNames.size(); ++i) {
        QJsonObject s;
        s["name"] = signalNames[i];
        QJsonArray vals;
            const auto& vec = data.y[indices[i]];
            for (int k : idx) vals.append(vec[k]);
            s["values"] = vals;
            sigArr.append(s);
    }
    out["signals"] = sigArr;
    return out;
}

static QString rawToCsv(const RawData& data, const QStringList& signalNames, const QVector<int>& indices, int maxPoints, double tStart, double tEnd, int baseSignalIndex = -1) {
    QString out;
    QTextStream stream(&out);
    stream << QString::fromStdString(data.varNames[0]);
    for (const auto& sig : signalNames) stream << "," << sig;
    stream << "\n";
    int baseSignal = baseSignalIndex;
    if (baseSignal < 0 || baseSignal >= data.y.size()) {
        baseSignal = indices.isEmpty() ? 0 : indices[0];
    }
    const QVector<int> idx = decimatedIndices(data, baseSignal, maxPoints, tStart, tEnd);
    for (int i : idx) {
        stream << data.x[i];
        for (int j = 0; j < indices.size(); ++j) {
            const auto& vec = data.y[indices[j]];
            if (i < vec.size()) stream << "," << vec[i];
            else stream << ",";
        }
        stream << "\n";
    }
    return out;
}

struct SignalStats {
    QString name;
    double min = 0.0;
    double max = 0.0;
    double avg = 0.0;
    double rms = 0.0;
};

static QVector<SignalStats> computeSignalStats(const RawData& data, const QStringList& signalNames, const QVector<int>& indices, const QVector<int>& sampleIndices) {
    QVector<SignalStats> stats;
    stats.reserve(indices.size());
    for (int i = 0; i < indices.size(); ++i) {
        const auto& vec = data.y[indices[i]];
        if (vec.empty()) continue;
        SignalStats s;
        s.name = signalNames[i];
        double sum = 0.0;
        double sumSq = 0.0;
        bool seeded = false;
        double minVal = 0.0;
        double maxVal = 0.0;
        if (sampleIndices.isEmpty()) {
            for (double v : vec) {
                if (!seeded) { minVal = v; maxVal = v; seeded = true; }
                sum += v;
                sumSq += v * v;
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }
            const int n = vec.size();
            s.min = minVal;
            s.max = maxVal;
            s.avg = sum / n;
            s.rms = std::sqrt(sumSq / n);
        } else {
            for (int idx : sampleIndices) {
                if (idx < 0 || idx >= vec.size()) continue;
                const double v = vec[idx];
                if (!seeded) { minVal = v; maxVal = v; seeded = true; }
                sum += v;
                sumSq += v * v;
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }
            const int n = sampleIndices.size();
            if (n > 0 && seeded) {
                s.min = minVal;
                s.max = maxVal;
                s.avg = sum / n;
                s.rms = std::sqrt(sumSq / n);
            } else {
                continue;
            }
        }
        stats.push_back(s);
    }
    return stats;
}

enum class MeasureType { Min, Max, Avg, Rms, Pp, At };

struct MeasureRequest {
    QString expr;
    QString signalName;
    MeasureType type;
    double atTime = 0.0;
};

static int findVarIndex(const QStringList& vars, const QString& name) {
    // Direct match
    for (int i = 0; i < vars.size(); ++i) {
        if (vars[i].compare(name, Qt::CaseInsensitive) == 0) return i;
    }
    // Fallback: try matching bare name against V(name) / I(name) entries
    for (int i = 0; i < vars.size(); ++i) {
        const QString& v = vars[i];
        if ((v.startsWith("V(", Qt::CaseInsensitive) || v.startsWith("I(", Qt::CaseInsensitive))
            && v.endsWith(")")) {
            const QString inner = v.mid(2, v.size() - 3);
            if (inner.compare(name, Qt::CaseInsensitive) == 0) return i;
        }
    }
    return -1;
}

static bool parseMeasure(const QString& expr, MeasureRequest* out, QString* error) {
    if (!out) return false;
    QString s = expr.trimmed();
    if (s.isEmpty()) {
        if (error) *error = "Empty measure expression.";
        return false;
    }
    MeasureRequest req;
    req.expr = s;

    int atPos = s.indexOf("@t=", Qt::CaseInsensitive);
    if (atPos >= 0) {
        const QString namePart = s.left(atPos).trimmed();
        const QString timePart = s.mid(atPos + 3).trimmed();
        double t = 0.0;
        if (!SimValueParser::parseSpiceNumber(timePart, t)) {
            if (error) *error = "Invalid time in measure: " + expr;
            return false;
        }
        req.type = MeasureType::At;
        req.atTime = t;
        s = namePart;
    } else if (s.endsWith("_min", Qt::CaseInsensitive)) {
        req.type = MeasureType::Min;
        s.chop(4);
    } else if (s.endsWith("_max", Qt::CaseInsensitive)) {
        req.type = MeasureType::Max;
        s.chop(4);
    } else if (s.endsWith("_avg", Qt::CaseInsensitive)) {
        req.type = MeasureType::Avg;
        s.chop(4);
    } else if (s.endsWith("_rms", Qt::CaseInsensitive)) {
        req.type = MeasureType::Rms;
        s.chop(4);
    } else if (s.endsWith("_pp", Qt::CaseInsensitive)) {
        req.type = MeasureType::Pp;
        s.chop(3);
    } else {
        req.type = MeasureType::Avg;
    }

    s = s.trimmed();
    // Keep the signal name as-is (V(5), I(L1), or bare name like "5")
    // findVarIndex handles both wrapped and bare-name matching
    req.signalName = s;
    if (req.signalName.isEmpty()) {
        if (error) *error = "Invalid measure signal: " + expr;
        return false;
    }
    *out = req;
    return true;
}

static int nearestIndex(const QVector<double>& xs, double t) {
    if (xs.isEmpty()) return -1;
    int lo = 0;
    int hi = xs.size() - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (xs[mid] < t) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return 0;
    if (lo >= xs.size()) return xs.size() - 1;
    const double d1 = std::abs(xs[lo] - t);
    const double d0 = std::abs(xs[lo - 1] - t);
    return (d0 <= d1) ? (lo - 1) : lo;
}

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QRectF standardSymbolBounds() {
    SymbolLibraryManager& libMgr = SymbolLibraryManager::instance();
    const SymbolDefinition* res = libMgr.findSymbol("Resistor");
    const SymbolDefinition* cap = libMgr.findSymbol("Capacitor");
    if (!res && !cap) return QRectF();
    if (res && cap) {
        const QRectF r = res->boundingRect();
        const QRectF c = cap->boundingRect();
        const qreal w = qMax(r.width(), c.width());
        const qreal h = qMax(r.height(), c.height());
        const QPointF center = QPointF(0.0, 0.0);
        return QRectF(center.x() - w * 0.5, center.y() - h * 0.5, w, h);
    }
    return res ? res->boundingRect() : cap->boundingRect();
}

QPointF scalePoint(const QPointF& p, const QPointF& fromCenter, const QPointF& toCenter, double scale) {
    return QPointF((p.x() - fromCenter.x()) * scale + toCenter.x(),
                   (p.y() - fromCenter.y()) * scale + toCenter.y());
}

void normalizeSymbolToStandardSize(SymbolDefinition& symbol) {
    const QRectF target = standardSymbolBounds();
    if (!target.isValid()) return;
    QRectF current = symbol.boundingRect();
    if (!current.isValid() || current.width() <= 0.0 || current.height() <= 0.0) return;

    const double scale = qMin(target.width() / current.width(), target.height() / current.height());
    if (!std::isfinite(scale) || scale <= 0.0) return;

    const QPointF fromCenter = current.center();
    const QPointF toCenter = target.center();

    for (SymbolPrimitive& prim : symbol.primitives()) {
        switch (prim.type) {
        case SymbolPrimitive::Line: {
            QPointF p1(prim.data["x1"].toDouble(), prim.data["y1"].toDouble());
            QPointF p2(prim.data["x2"].toDouble(), prim.data["y2"].toDouble());
            p1 = scalePoint(p1, fromCenter, toCenter, scale);
            p2 = scalePoint(p2, fromCenter, toCenter, scale);
            prim.data["x1"] = p1.x();
            prim.data["y1"] = p1.y();
            prim.data["x2"] = p2.x();
            prim.data["y2"] = p2.y();
            break;
        }
        case SymbolPrimitive::Rect:
        case SymbolPrimitive::Arc:
        case SymbolPrimitive::Image: {
            QRectF r(prim.data["x"].toDouble(), prim.data["y"].toDouble(),
                     prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble(),
                     prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble());
            QPointF tl = scalePoint(r.topLeft(), fromCenter, toCenter, scale);
            QPointF br = scalePoint(r.bottomRight(), fromCenter, toCenter, scale);
            QRectF nr(tl, br);
            prim.data["x"] = nr.x();
            prim.data["y"] = nr.y();
            if (prim.data.contains("width")) prim.data["width"] = nr.width();
            if (prim.data.contains("height")) prim.data["height"] = nr.height();
            if (prim.data.contains("w")) prim.data["w"] = nr.width();
            if (prim.data.contains("h")) prim.data["h"] = nr.height();
            break;
        }
        case SymbolPrimitive::Circle: {
            QPointF c(prim.data.contains("centerX") ? prim.data["centerX"].toDouble() : prim.data["cx"].toDouble(),
                      prim.data.contains("centerY") ? prim.data["centerY"].toDouble() : prim.data["cy"].toDouble());
            c = scalePoint(c, fromCenter, toCenter, scale);
            const double r = (prim.data.contains("radius") ? prim.data["radius"].toDouble() : prim.data["r"].toDouble()) * scale;
            if (prim.data.contains("centerX")) prim.data["centerX"] = c.x();
            if (prim.data.contains("centerY")) prim.data["centerY"] = c.y();
            if (prim.data.contains("cx")) prim.data["cx"] = c.x();
            if (prim.data.contains("cy")) prim.data["cy"] = c.y();
            if (prim.data.contains("radius")) prim.data["radius"] = r;
            if (prim.data.contains("r")) prim.data["r"] = r;
            break;
        }
        case SymbolPrimitive::Polygon: {
            QJsonArray pts = prim.data["points"].toArray();
            QJsonArray outPts;
            for (const auto& v : pts) {
                QJsonObject pt = v.toObject();
                QPointF p(pt["x"].toDouble(), pt["y"].toDouble());
                p = scalePoint(p, fromCenter, toCenter, scale);
                QJsonObject o;
                o["x"] = p.x();
                o["y"] = p.y();
                outPts.append(o);
            }
            prim.data["points"] = outPts;
            break;
        }
        case SymbolPrimitive::Text: {
            QPointF p(prim.data["x"].toDouble(), prim.data["y"].toDouble());
            p = scalePoint(p, fromCenter, toCenter, scale);
            prim.data["x"] = p.x();
            prim.data["y"] = p.y();
            if (prim.data.contains("fontSize")) {
                prim.data["fontSize"] = prim.data["fontSize"].toDouble() * scale;
            }
            break;
        }
        case SymbolPrimitive::Pin: {
            QPointF p(prim.data["x"].toDouble(), prim.data["y"].toDouble());
            p = scalePoint(p, fromCenter, toCenter, scale);
            prim.data["x"] = p.x();
            prim.data["y"] = p.y();
            if (prim.data.contains("length")) prim.data["length"] = prim.data["length"].toDouble() * scale;
            if (prim.data.contains("nameSize")) prim.data["nameSize"] = prim.data["nameSize"].toDouble() * scale;
            if (prim.data.contains("numSize")) prim.data["numSize"] = prim.data["numSize"].toDouble() * scale;
            break;
        }
        case SymbolPrimitive::Bezier: {
            for (int i = 1; i <= 4; ++i) {
                QPointF p(prim.data[QString("x%1").arg(i)].toDouble(),
                          prim.data[QString("y%1").arg(i)].toDouble());
                p = scalePoint(p, fromCenter, toCenter, scale);
                prim.data[QString("x%1").arg(i)] = p.x();
                prim.data[QString("y%1").arg(i)] = p.y();
            }
            break;
        }
        default:
            break;
        }
    }

    symbol.setReferencePos(scalePoint(symbol.referencePos(), fromCenter, toCenter, scale));
    symbol.setNamePos(scalePoint(symbol.namePos(), fromCenter, toCenter, scale));
}

QString normalizeNetlistLine(const QString& line) {
    QString s = line;
    const int commentIdx = s.indexOf(';');
    if (commentIdx >= 0) s = s.left(commentIdx);
    s = s.trimmed();
    if (s.isEmpty()) return QString();
    s.replace(QRegularExpression("\\s+"), " ");
    return s.toUpper();
}

QStringList normalizeNetlistText(const QString& text) {
    QStringList lines = text.split('\n');
    QStringList out;
    QString current;
    for (QString line : lines) {
        line.replace('\r', "");
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        if (trimmed.startsWith('*') || trimmed.startsWith(';')) continue;
        if (trimmed.startsWith('+')) {
            const QString cont = normalizeNetlistLine(trimmed.mid(1));
            if (!cont.isEmpty() && !current.isEmpty()) {
                current += " " + cont;
            }
            continue;
        }
        if (!current.isEmpty()) {
            out.append(current);
            current.clear();
        }
        current = normalizeNetlistLine(trimmed);
    }
    if (!current.isEmpty()) out.append(current);
    return out;
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

Qt::PenStyle parseLineStyle(const QString& style) {
    const QString s = style.trimmed().toLower();
    if (s == "dash") return Qt::DashLine;
    if (s == "dot") return Qt::DotLine;
    if (s == "dashdot") return Qt::DashDotLine;
    return Qt::SolidLine;
}

QColor parseColorOrDefault(const QJsonObject& data, const QString& key, const QColor& fallback) {
    if (data.contains(key)) {
        QColor c(data.value(key).toString());
        if (c.isValid()) return c;
    }
    return fallback;
}

bool renderSymbolToPng(const SymbolDefinition& symbol, const QString& outPath, bool transparent = false, qreal scale = 4.0) {
    QRectF rect = symbol.boundingRect();
    if (rect.isNull() || rect.width() <= 0 || rect.height() <= 0) {
        rect = QRectF(-20, -20, 40, 40);
    }

    const qreal margin = 10.0;
    QSize imageSize = QSize(qCeil((rect.width() + margin * 2.0) * scale),
                            qCeil((rect.height() + margin * 2.0) * scale));

    QImage image(imageSize, QImage::Format_ARGB32);
    image.fill(transparent ? Qt::transparent : QColor(30, 30, 30));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate((-rect.left() + margin) * scale, (-rect.top() + margin) * scale);
    painter.scale(scale, scale);

    const QColor lineColor(220, 220, 220);
    const QColor textColor(230, 230, 230);

    for (const SymbolPrimitive& prim : symbol.primitives()) {
        const QJsonObject& d = prim.data;
        switch (prim.type) {
        case SymbolPrimitive::Line: {
            qreal w = d.value("lineWidth").toDouble(1.5);
            QPen pen(lineColor, w, parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(d.value("x1").toDouble(), d.value("y1").toDouble()),
                             QPointF(d.value("x2").toDouble(), d.value("y2").toDouble()));
            break;
        }
        case SymbolPrimitive::Rect: {
            qreal x = d.value("x").toDouble();
            qreal y = d.value("y").toDouble();
            qreal w = d.contains("width") ? d.value("width").toDouble() : d.value("w").toDouble();
            qreal h = d.contains("height") ? d.value("height").toDouble() : d.value("h").toDouble();
            bool filled = d.value("filled").toBool(false);
            QPen pen(lineColor, d.value("lineWidth").toDouble(1.5), parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(filled ? parseColorOrDefault(d, "fillColor", QColor(255, 255, 255, 30)) : Qt::NoBrush);
            painter.drawRect(QRectF(x, y, w, h));
            break;
        }
        case SymbolPrimitive::Circle: {
            qreal cx = d.value("cx").toDouble();
            qreal cy = d.value("cy").toDouble();
            qreal r = d.value("r").toDouble();
            bool filled = d.value("filled").toBool(false);
            QPen pen(lineColor, d.value("lineWidth").toDouble(1.5), parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(filled ? parseColorOrDefault(d, "fillColor", QColor(255, 255, 255, 30)) : Qt::NoBrush);
            painter.drawEllipse(QPointF(cx, cy), r, r);
            break;
        }
        case SymbolPrimitive::Arc: {
            qreal x = d.value("x").toDouble();
            qreal y = d.value("y").toDouble();
            qreal w = d.contains("width") ? d.value("width").toDouble() : d.value("w").toDouble();
            qreal h = d.contains("height") ? d.value("height").toDouble() : d.value("h").toDouble();
            int sa = d.contains("startAngle") ? d.value("startAngle").toInt() : d.value("start").toInt();
            int sp = d.contains("spanAngle") ? d.value("spanAngle").toInt() : d.value("span").toInt();
            QPen pen(lineColor, d.value("lineWidth").toDouble(1.5), parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawArc(QRectF(x, y, w, h), sa, sp);
            break;
        }
        case SymbolPrimitive::Polygon: {
            QJsonArray pts = d.value("points").toArray();
            QPolygonF poly;
            for (const auto& v : pts) {
                QJsonObject pt = v.toObject();
                poly << QPointF(pt.value("x").toDouble(), pt.value("y").toDouble());
            }
            bool filled = d.value("filled").toBool(false);
            QPen pen(lineColor, d.value("lineWidth").toDouble(1.5), parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(filled ? parseColorOrDefault(d, "fillColor", QColor(255, 255, 255, 30)) : Qt::NoBrush);
            painter.drawPolygon(poly);
            break;
        }
        case SymbolPrimitive::Bezier: {
            QPainterPath path;
            path.moveTo(d.value("x1").toDouble(), d.value("y1").toDouble());
            path.cubicTo(QPointF(d.value("x2").toDouble(), d.value("y2").toDouble()),
                         QPointF(d.value("x3").toDouble(), d.value("y3").toDouble()),
                         QPointF(d.value("x4").toDouble(), d.value("y4").toDouble()));
            QPen pen(lineColor, d.value("lineWidth").toDouble(1.5), parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
            break;
        }
        case SymbolPrimitive::Text: {
            const QString text = d.value("text").toString();
            const qreal x = d.value("x").toDouble();
            const qreal y = d.value("y").toDouble();
            int fs = d.value("fontSize").toInt(10);
            QFont font("SansSerif", fs);
            painter.setFont(font);
            painter.setPen(parseColorOrDefault(d, "color", textColor));
            const qreal rot = d.value("rotation").toDouble(0.0);
            if (std::abs(rot) > 1e-6) {
                painter.save();
                painter.translate(x, y);
                painter.rotate(-rot);
                painter.drawText(QPointF(0, 0), text);
                painter.restore();
            } else {
                painter.drawText(QPointF(x, y), text);
            }
            break;
        }
        case SymbolPrimitive::Pin: {
            const qreal px = d.value("x").toDouble();
            const qreal py = d.value("y").toDouble();
            qreal len = d.value("length").toDouble(15.0);
            if (len <= 0) len = 15.0;
            const QString orient = d.value("orientation").toString("Right");
            QPointF endPt(px + len, py);
            if (orient == "Left") endPt = QPointF(px - len, py);
            else if (orient == "Up") endPt = QPointF(px, py - len);
            else if (orient == "Down") endPt = QPointF(px, py + len);
            QPen pen(lineColor, 2.0);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(px, py), endPt);
            painter.setBrush(lineColor);
            painter.drawEllipse(QPointF(px, py), 2.5, 2.5);

            const bool hideNum = d.value("hideNum").toBool(false);
            const bool hideName = d.value("hideName").toBool(false);
            const QString num = d.contains("number") ? QString::number(d.value("number").toInt()) : d.value("num").toString();
            const QString name = d.value("name").toString();
            int nsz = d.value("numSize").toInt(7);
            int asz = d.value("nameSize").toInt(7);
            if (!hideNum && !num.isEmpty()) {
                painter.setFont(QFont("Monospace", nsz > 0 ? nsz : 7));
                painter.setPen(textColor);
                painter.drawText(QPointF(px + 2, py - 2), num);
            }
            if (!hideName && !name.isEmpty()) {
                painter.setFont(QFont("SansSerif", asz > 0 ? asz : 7));
                painter.setPen(textColor);
                painter.drawText(QPointF(endPt.x() + 2, endPt.y() - 2), name);
            }
            break;
        }
        case SymbolPrimitive::Image: {
            QString base64 = d.value("image").toString();
            if (!base64.isEmpty()) {
                QByteArray bytes = QByteArray::fromBase64(base64.toLatin1());
                QImage img;
                img.loadFromData(bytes);
                if (!img.isNull()) {
                    qreal x = d.value("x").toDouble();
                    qreal y = d.value("y").toDouble();
                    qreal w = d.contains("width") ? d.value("width").toDouble() : d.value("w").toDouble();
                    qreal h = d.contains("height") ? d.value("height").toDouble() : d.value("h").toDouble();
                    painter.drawImage(QRectF(x, y, w, h), img);
                }
            }
            break;
        }
        }
    }

    painter.end();
    return image.save(outPath);
}

struct SpiceEntity {
    QString type; // ".subckt" or ".model"
    QString name;
    QStringList pins;
    QString modelType; // For .model: D, NPN, PNP, etc.
};

static QStringList collapseContinuationLines(const QString& text) {
    QStringList collapsed;
    QString current;
    const QStringList lines = text.split('\n');
    for (const QString& rawLine : lines) {
        const QString trimmed = rawLine.trimmed();
        if (trimmed.startsWith('+')) {
            const QString continuation = trimmed.mid(1).trimmed();
            if (current.isEmpty()) {
                current = continuation;
            } else if (!continuation.isEmpty()) {
                if (!current.endsWith(' ')) current += ' ';
                current += continuation;
            }
            continue;
        }
        if (!current.isEmpty()) {
            collapsed.append(current);
            current.clear();
        }
        if (!trimmed.isEmpty()) {
            current = trimmed;
        }
    }
    if (!current.isEmpty()) {
        collapsed.append(current);
    }
    return collapsed;
}

static QList<SpiceEntity> parseSpiceEntities(const QString& text) {
    const QStringList lines = collapseContinuationLines(text);
    QList<SpiceEntity> parsed;
    for (const QString& line : lines) {
        if (!line.startsWith('.', Qt::CaseInsensitive)) continue;
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        
        if (parts.first().compare(".subckt", Qt::CaseInsensitive) == 0 && parts.size() >= 2) {
            SpiceEntity ent;
            ent.type = ".subckt";
            ent.name = parts.at(1);
            for (int i = 2; i < parts.size(); ++i) {
                if (parts.at(i).contains('=')) break;
                ent.pins.append(parts.at(i));
            }
            parsed.append(ent);
        } else if (parts.first().compare(".model", Qt::CaseInsensitive) == 0 && parts.size() >= 3) {
            SpiceEntity ent;
            ent.type = ".model";
            ent.name = parts.at(1);
            QString mType = parts.at(2);
            if (mType.contains('(')) mType = mType.left(mType.indexOf('('));
            ent.modelType = mType.toUpper();
            parsed.append(ent);
        }
    }
    return parsed;
}

struct MappingRule {
    QString name;
    QString templateName;
    QString spiceType;
    int pinCount = -1;
    QRegularExpression nameRegex;
    QStringList modelTypes;
};

class SymbolMatcher {
public:
    bool loadMapping(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return false;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) return false;
        
        QJsonArray rulesArr = doc.object().value("rules").toArray();
        for (const QJsonValue& val : rulesArr) {
            QJsonObject obj = val.toObject();
            QJsonObject match = obj.value("match").toObject();
            
            MappingRule rule;
            rule.name = obj.value("name").toString();
            rule.templateName = obj.value("template").toString();
            rule.spiceType = match.value("spice_type").toString();
            rule.pinCount = match.value("pin_count").toInt(-1);
            QString regexStr = match.value("name_regex").toString();
            if (!regexStr.isEmpty()) {
                rule.nameRegex = QRegularExpression(regexStr, QRegularExpression::CaseInsensitiveOption);
            }
            
            QJsonValue mType = match.value("model_type");
            if (mType.isArray()) {
                for (const QJsonValue& v : mType.toArray()) rule.modelTypes << v.toString().toUpper();
            } else if (!mType.isUndefined()) {
                rule.modelTypes << mType.toString().toUpper();
            }
            
            m_rules << rule;
        }
        return true;
    }

    QString match(const SpiceEntity& ent) const {
        for (const auto& rule : m_rules) {
            if (!rule.spiceType.isEmpty() && rule.spiceType != ent.type) continue;
            if (rule.pinCount != -1 && rule.pinCount != (int)ent.pins.size() && ent.type == ".subckt") continue;
            
            if (ent.type == ".model") {
                if (!rule.modelTypes.isEmpty() && !rule.modelTypes.contains(ent.modelType.toUpper())) continue;
            }
            
            if (rule.nameRegex.isValid()) {
                auto res = rule.nameRegex.match(ent.name);
                if (!res.hasMatch()) {
                    // Fallback to case-insensitive
                    auto matchCi = QRegularExpression(rule.nameRegex.pattern(), QRegularExpression::CaseInsensitiveOption).match(ent.name);
                    if (!matchCi.hasMatch()) continue;
                }
            }
            
            if (g_debug) std::cout << "Matched " << ent.name.toStdString() << " to " << rule.templateName.toStdString() << std::endl;
            return rule.templateName;
        }
        if (g_debug) std::cout << "Failed to match " << ent.name.toStdString() << " (Type: " << ent.type.toStdString() << ", Pins: " << ent.pins.size() << ")" << std::endl;
        return "";
    }

private:
    QList<MappingRule> m_rules;
};

static int generateSymbolsForLibrary(const QString& inputPath, const QString& outDir, const QString& symbolType, const QString& targetName = QString(), const SymbolMatcher* matcher = nullptr) {
    QFile file(inputPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "Error: Cannot read input file: " << inputPath.toStdString() << std::endl;
        return 0;
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    QList<SpiceEntity> entities = parseSpiceEntities(content);
    if (entities.isEmpty()) {
        return 0;
    }

    QDir().mkpath(outDir);
    int count = 0;

    for (const auto& sub : entities) {
        if (!targetName.isEmpty() && sub.name.compare(targetName, Qt::CaseInsensitive) != 0) continue;

        auto getPinName = [&](int i) -> QString {
            if (i >= 0 && i < sub.pins.size()) return sub.pins[i];
            return QString::number(i + 1);
        };

        QString typeToUse = symbolType;
        if (typeToUse.isEmpty() && matcher) {
            typeToUse = matcher->match(sub);
        }

        SymbolDefinition def(sub.name);
        def.setDescription(QString("Auto-generated %1 symbol for %2 %3").arg(typeToUse.isEmpty() ? "IC" : typeToUse.toUpper(), sub.type, sub.name));
        def.setCategory(typeToUse == "op" ? "Amplifiers" : "Integrated Circuits");
        def.setReferencePrefix(typeToUse == "op" ? "U" : "U");
        def.setDefaultValue(sub.name);
        def.setSpiceModelName(sub.name);
        def.setModelSource("project");
        def.setModelPath(QFileInfo(inputPath).fileName());

        int pinCount = sub.pins.size();
        if (sub.type == ".model") {
            if (sub.modelType == "D") pinCount = 2;
            else if (sub.modelType == "NPN" || sub.modelType == "PNP" || sub.modelType == "NJF" || sub.modelType == "PJF") pinCount = 3;
            else if (sub.modelType == "NMOS" || sub.modelType == "PMOS" || sub.modelType == "VDMOS") pinCount = 3;
            else if (sub.modelType == "SW" || sub.modelType == "CSW") pinCount = 2;
        }
        const qreal bodyWidth = 90.0;
        const qreal pinSpacing = 30.0;
        const qreal pinLength = 15.0;

        if (typeToUse == "triode") {
            def.setCategory("Vacuum Tubes");
            def.setReferencePrefix("V");
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 37.5, false)); // Envelope
            // Plate (Anode)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -22.5), QPointF(15, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -22.5), QPointF(0, -45)));
            // Grid (Dashed)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-7.5, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-3.75, 0), QPointF(3.75, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, 0), QPointF(15, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-45, 0)));
            // Cathode
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 22.5), QPointF(11.25, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 22.5), QPointF(-11.25, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(0, 45)));
            // Heater (Filament) - if 5 pins (A G K H1 H2)
            if (pinCount >= 5) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, 37.5), QPointF(0, 26.25)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, 37.5), QPointF(0, 26.25)));
            }
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 2, "G", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 3, "K", "Up", 0));
            if (pinCount >= 5) {
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-15, 45), 4, "H1", "Up", 0));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, 45), 5, "H2", "Up", 0));
            }
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "pentode") {
            def.setCategory("Vacuum Tubes");
            def.setReferencePrefix("V");
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 37.5, false));
            // Plate (Anode)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -22.5), QPointF(15, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -22.5), QPointF(0, -45)));
            // Grids (G3 suppressed, G2 screen, G1 control)
            for (int i = 0; i < 3; ++i) {
                qreal y = -11.25 + i * 11.25;
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, y), QPointF(-7.5, y)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-3.75, y), QPointF(3.75, y)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, y), QPointF(15, y)));
            }
            // Grid Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(-45, 11.25))); // G1 (Control)
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(45, 0))); // G2 (Screen)
            if (pinCount >= 5) def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -11.25), QPointF(45, -11.25))); // G3 (Suppressor)
            // Cathode
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 22.5), QPointF(11.25, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 22.5), QPointF(-11.25, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(0, 45)));
            // Heater (Filament) - if 6+ pins
            if (pinCount >= 6) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, 37.5), QPointF(0, 26.25)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, 37.5), QPointF(0, 26.25)));
            }
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 2, "G2", "Left", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 11.25), 3, "G1", "Right", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 4, "K", "Up", 0));
            if (pinCount >= 5) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -11.25), 5, "G3", "Left", 0));
            if (pinCount >= 7) {
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-15, 45), 6, "H1", "Up", 0));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, 45), 7, "H2", "Up", 0));
            }
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "zener") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("DZ");
            // Cathode bar with bends
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 11.25), QPointF(15, 18.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(-15, 3.75)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "schottky") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("DS");
            // Cathode bar with hooks
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 11.25), QPointF(15, 3.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 3.75), QPointF(7.5, 3.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(-15, 18.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 18.75), QPointF(-7.5, 18.75)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "varicap") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("DV");
            // Double line at cathode (capacitor look)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 18.75), QPointF(15, 18.75)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 18.75), QPointF(0, 37.5)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 37.5), 2, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "inductor") {
            def.setCategory("Passives");
            def.setReferencePrefix("L");
            // 4 arcs for inductor
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-30, -7.5, 15, 15), 0, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-15, -7.5, 15, 15), 0, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(0, -7.5, 15, 15), 0, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(15, -7.5, 15, 15), 0, 180 * 16));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, "1", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 2, "2", "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "adc" || typeToUse == "dac") {
            def.setCategory("Data Converters");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-45, -45, 90, 90), false));
            def.addPrimitive(SymbolPrimitive::createText(symbolType.toUpper(), QPointF(-15, -10), 10));
            QMap<int, QString> mapping;
            // Generic labeled box
            for (int i = 0; i < qMin(pinCount, 8); ++i) {
                qreal y = -30 + (i % 4) * 20;
                bool left = (i < 4);
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(left ? -45 : 45, y), QPointF(left ? -60 : 60, y)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(left ? -60 : 60, y), i + 1, QString("P%1").arg(i+1), left ? "Right" : "Left", 0));
                mapping.insert(i + 1, getPinName(i));
            }
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "njfet" || typeToUse == "pjfet") {
            const bool pjfet = (typeToUse == "pjfet");
            def.setCategory("Semiconductors");
            def.setReferencePrefix("J");
            // Channel bar
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -30), QPointF(0, 30)));
            // Gate terminal
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(0, 0)));
            // Drain/Source terminals
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -22.5), QPointF(30, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -22.5), QPointF(30, -45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(30, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 22.5), QPointF(30, 45)));
            // Arrow
            QList<QPointF> arrow;
            if (pjfet) { // Points AWAY from channel
                arrow << QPointF(-22.5, 0) << QPointF(-7.5, -7.5) << QPointF(-7.5, 7.5);
            } else { // Points INTO channel
                arrow << QPointF(-7.5, 0) << QPointF(-22.5, -7.5) << QPointF(-22.5, 7.5);
            }
            def.addPrimitive(SymbolPrimitive::createPolygon(arrow, false));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, -45), 1, "D", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 2, "G", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 45), 3, "S", "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "optocoupler_4pin") {
            def.setCategory("Optoelectronics");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-45, -37.5, 90, 75), false));
            // Internal LED
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -7.5), QPointF(-30, 7.5))); // Cathode bar
            QList<QPointF> tri; tri << QPointF(-40, -7.5) << QPointF(-20, -7.5) << QPointF(-30, 7.5);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            // Internal Phototransistor (NPN)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -15), QPointF(15, 15))); // Base bar
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -7.5), QPointF(30, -22.5))); // Collector
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 7.5), QPointF(30, 22.5)));   // Emitter
            // Emitter Arrow
            QList<QPointF> eArrow; eArrow << QPointF(30, 22.5) << QPointF(22.5, 11.25) << QPointF(15, 18.75);
            def.addPrimitive(SymbolPrimitive::createPolygon(eArrow, false));
            // Opto Arrows (with tips)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, -7.5), QPointF(5, -7.5)));
            QList<QPointF> tip1; tip1 << QPointF(5, -7.5) << QPointF(0, -11.25) << QPointF(0, -3.75);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip1, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-10, 7.5), QPointF(5, 7.5)));
            QList<QPointF> tip2; tip2 << QPointF(5, 7.5) << QPointF(0, 3.75) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip2, false));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -7.5), QPointF(-30, -52.5))); // Anode
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 7.5), QPointF(-30, 52.5)));   // Cathode
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -22.5), QPointF(30, -52.5)));  // Collector
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 22.5), QPointF(30, 52.5)));    // Emitter
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, -52.5), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 52.5), 2, "K", "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, -52.5), 3, "C", "Down", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 52.5), 4, "E", "Up", 0));
            for(int i=0; i<qMin(pinCount, 4); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "photodiode") {
            def.setCategory("Optoelectronics");
            def.setReferencePrefix("D");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25))); // Cathode bar
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            // Incoming Arrows (with tips)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -30), QPointF(-15, -15)));
            QList<QPointF> tip1; tip1 << QPointF(-15, -15) << QPointF(-22.5, -15) << QPointF(-15, -22.5);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip1, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-37.5, -22.5), QPointF(-22.5, -7.5)));
            QList<QPointF> tip2; tip2 << QPointF(-22.5, -7.5) << QPointF(-30, -7.5) << QPointF(-22.5, -15);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip2, false));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -37.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 37.5)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -37.5), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 37.5), 2, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "tl431") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("U");
            // Classic Adjustable Zener
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 11.25), QPointF(15, 18.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(-15, 3.75)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            // Reference lead (diagonal to control junction)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-11.25, 0)));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 1, "K", "Up", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 2, "REF", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 3, "A", "Down", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "op") {
            def.setCategory("Amplifiers");
            def.setReferencePrefix("U");
            QStringList nodes = sub.pins;
            auto findPin = [&](const QStringList& names, const QString& pattern) -> int {
                QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
                for (int i = 0; i < names.size(); ++i) {
                    if (re.match(names[i]).hasMatch()) return i + 1;
                }
                return -1;
            };
            int idxINP = findPin(nodes, "in\\+|\\+in|inp");
            int idxINN = findPin(nodes, "in\\-|\\-in|inn");
            int idxOUT = findPin(nodes, "out");
            int idxVCC = findPin(nodes, "vcc|v\\+|vdd|vp");
            int idxVEE = findPin(nodes, "vss|v\\-|vee|vn");

            // Fallback for standard OpAmp order: IN+, IN-, V+, V-, OUT
            if (idxINP == -1 && nodes.size() >= 1) idxINP = 1;
            if (idxINN == -1 && nodes.size() >= 2) idxINN = 2;
            if (idxVCC == -1 && nodes.size() >= 3) idxVCC = 3;
            if (idxVEE == -1 && nodes.size() >= 4) idxVEE = 4;
            if (idxOUT == -1 && nodes.size() >= 5) idxOUT = 5;

            if (pinCount <= 5) {
                // Single OpAmp (Traditional Triangle)
                QList<QPointF> tri; tri << QPointF(-20, -25) << QPointF(-20, 25) << QPointF(20, 0);
                def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
                def.addPrimitive(SymbolPrimitive::createText("+", QPointF(-17, 10), 8));
                def.addPrimitive(SymbolPrimitive::createText("-", QPointF(-17, -15), 10));
                
                if (idxINN != -1) {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, -12.5), QPointF(-40, -12.5)));
                    def.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, -12.5), idxINN, "IN-", "Right", 0));
                }
                if (idxINP != -1) {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, 12.5), QPointF(-40, 12.5)));
                    def.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, 12.5), idxINP, "IN+", "Right", 0));
                }
                if (idxOUT != -1) {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(20, 0), QPointF(40, 0)));
                    def.addPrimitive(SymbolPrimitive::createPin(QPointF(40, 0), idxOUT, "OUT", "Left", 0));
                }
                if (idxVCC != -1) {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -12.5), QPointF(0, -30)));
                    def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), idxVCC, "V+", "Down", 0));
                }
                if (idxVEE != -1) {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 12.5), QPointF(0, 30)));
                    def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), idxVEE, "V-", "Up", 0));
                }
            } else {
                // Multi-OpAmp package (Dual/Quad)
                const int ampCount = (pinCount >= 14) ? 4 : 2;
                const qreal h = ampCount * 40.0;
                def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -h/2, 60, h), false));
                for (int i=0; i<ampCount; ++i) {
                    qreal yOff = -h/2 + 20 + i*40;
                    QList<QPointF> tri; 
                    tri << QPointF(-15, yOff-10) << QPointF(-15, yOff+10) << QPointF(5, yOff);
                    def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
                    def.addPrimitive(SymbolPrimitive::createText(QString::number(i+1), QPointF(10, yOff-5), 6));
                }
                const int half = (pinCount+1)/2;
                for (int i = 0; i < pinCount; ++i) {
                    bool left = i < half;
                    qreal y = -h/2 + 10 + (left ? i : (pinCount - 1 - i)) * 20;
                    QPointF pos(left ? -45 : 45, y);
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(left?-30:30, y), pos));
                    def.addPrimitive(SymbolPrimitive::createPin(pos, i+1, getPinName(i), left?"Right":"Left", 0));
                }
            }
            QMap<int, QString> mapping;
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "npn" || typeToUse == "pnp") {
            const bool pnp = (typeToUse == "pnp");
            def.setCategory("Semiconductors");
            def.setReferencePrefix(pnp ? "QP" : "QN");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -30), QPointF(0, 30))); // Base bar
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(0, 0))); // Base terminal
            
            // Collector: Diagonal + Vertical Post
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -15), QPointF(45, -45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(45, -45), QPointF(45, -60)));
            
            // Emitter: Diagonal + Vertical Post
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 15), QPointF(45, 45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(45, 45), QPointF(45, 60)));
            
            // Arrow (official positions)
            QList<QPointF> arrow;
            if (pnp) {
                arrow << QPointF(0, 15) << QPointF(26.25, 26.25) << QPointF(18.75, 33.75);
            } else {
                arrow << QPointF(45, 45) << QPointF(26.25, 26.25) << QPointF(18.75, 33.75);
            }
            def.addPrimitive(SymbolPrimitive::createPolygon(arrow, false));
            
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -60), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 2, getPinName(1), "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 60), 3, getPinName(2), "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "nmos" || typeToUse == "pmos") {
            const bool pmos = (typeToUse == "pmos");
            def.setCategory("Semiconductors");
            def.setReferencePrefix(pmos ? "MP" : "MN");
            
            // 3-segment dashed channel (at x=0)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -37.5), QPointF(0, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -7.5), QPointF(0, 7.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(0, 37.5)));
            
            // Gate bar (at x=-7.5)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, -30), QPointF(-7.5, 30)));
            // Gate terminal
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 0), QPointF(-7.5, 0)));
            
            // Drain terminal
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -30), QPointF(30, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -30), QPointF(30, -45)));
            
            // Source terminal
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 30), QPointF(30, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 30), QPointF(30, 45)));
            
            // Source-Bulk tie
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 0), QPointF(30, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(30, 30)));
            
            QList<QPointF> arrow;
            if (pmos) {
                arrow << QPointF(7.5, 0) << QPointF(0, -3.75) << QPointF(0, 3.75); // Arrow at bulk start
            } else {
                arrow << QPointF(0, 0) << QPointF(7.5, -3.75) << QPointF(7.5, 3.75); // Arrow at bulk tip
            }
            def.addPrimitive(SymbolPrimitive::createPolygon(arrow, false));
            
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, -45), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-22.5, 0), 2, getPinName(1), "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 45), 3, getPinName(2), "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "diode") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("D");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25))); // Cathode bar
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            
            // Vertical leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));

            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, getPinName(1), "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "led") {
            def.setCategory("Optoelectronics");
            def.setReferencePrefix("D");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25))); // Cathode bar
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            // Emission Arrows (with tips)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -15), QPointF(30, -30)));
            QList<QPointF> tip1; tip1 << QPointF(30, -30) << QPointF(22.5, -30) << QPointF(30, -22.5);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip1, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, -7.5), QPointF(37.5, -22.5)));
            QList<QPointF> tip2; tip2 << QPointF(37.5, -22.5) << QPointF(30, -22.5) << QPointF(37.5, -15);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip2, false));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "triac") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("TR");
            QList<QPointF> tri1; tri1 << QPointF(-15, -15) << QPointF(15, -15) << QPointF(0, 0);
            QList<QPointF> tri2; tri2 << QPointF(-15, 15) << QPointF(15, 15) << QPointF(0, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri1, false));
            def.addPrimitive(SymbolPrimitive::createPolygon(tri2, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(15, 0))); // Middle bar
            // Gate (connected to MT1 junction side)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, 7.5), QPointF(22.5, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, 22.5), QPointF(45, 22.5)));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -15), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 15), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "MT2", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 22.5), 2, "G", "Left", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 3, "MT1", "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "scr") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("SCR");
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25))); // Cathode bar
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            // Gate (connected near cathode junction)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(15, 22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 22.5), QPointF(45, 22.5)));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "A", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 22.5), 2, "G", "Left", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 3, "K", "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "diac") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("DIA");
            QList<QPointF> tri1; tri1 << QPointF(-15, -15) << QPointF(15, -15) << QPointF(0, 0);
            QList<QPointF> tri2; tri2 << QPointF(-15, 15) << QPointF(15, 15) << QPointF(0, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri1, false));
            def.addPrimitive(SymbolPrimitive::createPolygon(tri2, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(15, 0)));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -15), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 15), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, getPinName(1), "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "igbt") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("QG");
            // Channel/Gate structure
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -22.5), QPointF(0, 22.5))); // Channel bar
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, -22.5), QPointF(-7.5, 22.5))); // Gate bar
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 0), QPointF(-7.5, 0))); // Gate lead
            // Collector
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(30, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -30), QPointF(30, -45)));
            // Emitter
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(30, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 30), QPointF(30, 45)));
            // Emitter Arrow (into the terminal)
            QList<QPointF> arrow; arrow << QPointF(30, 30) << QPointF(18.75, 22.5) << QPointF(15, 33.75);
            def.addPrimitive(SymbolPrimitive::createPolygon(arrow, false));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, -45), 1, "C", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-22.5, 0), 2, "G", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 45), 3, "E", "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "darlington_npn") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("QD");
            // First NPN
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -15), QPointF(-15, 15))); // Base bar 1
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-15, 0))); // Base in
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -7.5), QPointF(7.5, -22.5))); // Collector 1
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 7.5), QPointF(0, 18.75))); // Emitter 1 -> Base 2
            // Second NPN
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 3.75), QPointF(0, 33.75))); // Base bar 2
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(15, 0))); // Collector 2
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 26.25), QPointF(15, 37.5))); // Emitter 2
            // Collector connection
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, -22.5), QPointF(15, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -22.5), QPointF(15, 0)));
            // External Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -22.5), QPointF(15, -45))); // C
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 37.5), QPointF(15, 52.5)));  // E
            // Arrow on Q2 emitter
            QList<QPointF> arrow; arrow << QPointF(15, 37.5) << QPointF(7.5, 26.25) << QPointF(0, 33.75);
            def.addPrimitive(SymbolPrimitive::createPolygon(arrow, false));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, -45), 1, "C", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 2, "B", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, 52.5), 3, "E", "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "tvs_bidir") {
            def.setCategory("Protective");
            def.setReferencePrefix("D");
            // Two triangles pointing at each other
            QList<QPointF> tri1; tri1 << QPointF(-11.25, -15) << QPointF(11.25, -15) << QPointF(0, 0);
            QList<QPointF> tri2; tri2 << QPointF(-11.25, 15) << QPointF(11.25, 15) << QPointF(0, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri1, false));
            def.addPrimitive(SymbolPrimitive::createPolygon(tri2, false));
            // Back-to-back Zener bars
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 0), QPointF(11.25, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 0), QPointF(-11.25, -7.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(11.25, 0), QPointF(11.25, 7.5)));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -15), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 15), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, getPinName(1), "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "varistor") {
            def.setCategory("Protective");
            def.setReferencePrefix("RV");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-22.5, -7.5, 45, 15), false));
            // Varistor diagonal
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 15), QPointF(22.5, -15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 15), QPointF(-30, 15))); // Hook left
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, -15), QPointF(30, -15))); // Hook right
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 0), QPointF(-37.5, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, 0), QPointF(37.5, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-37.5, 0), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(37.5, 0), 2, getPinName(1), "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "oscillator_4pin") {
            def.setCategory("Oscillators");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -30, 60, 60), false));
            // Crystal symbol inside
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-7.5, -11.25, 15, 22.5), false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, -15), QPointF(-11.25, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(11.25, -15), QPointF(11.25, 15)));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-45, -15))); // VCC/NC
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 15), QPointF(-45, 15)));  // GND
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -15), QPointF(45, -15)));  // OUT
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 15), QPointF(45, 15)));   // OE/NC
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), 1, "VCC", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), 2, "GND", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 15), 3, "OUT", "Left", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -15), 4, "OE", "Left", 0));
            for(int i=0; i<qMin(pinCount, 4); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "vref_series") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -30, 60, 60), false));
            def.addPrimitive(SymbolPrimitive::createText("VREF", QPointF(-20, -10), 8));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0))); // IN
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));  // OUT
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 30), QPointF(0, 45)));   // GND
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, "IN", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 2, "GND", "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 3, "OUT", "Left", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "current_source") {
            def.setCategory("Simulation");
            def.setReferencePrefix("I");
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 22.5, false));
            // Arrow inside
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, 11.25)));
            QList<QPointF> tip; tip << QPointF(0, 11.25) << QPointF(-3.75, 3.75) << QPointF(3.75, 3.75);
            def.addPrimitive(SymbolPrimitive::createPolygon(tip, false));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -22.5), QPointF(0, -37.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 22.5), QPointF(0, 37.5)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -37.5), 1, "+", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 37.5), 2, "-", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "antenna") {
            def.setCategory("Miscellaneous");
            def.setReferencePrefix("ANT");
            // Triangular antenna
            QList<QPointF> tri; tri << QPointF(0, 0) << QPointF(-15, -15) << QPointF(15, -15);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 0), QPointF(0, 15)));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 15), QPointF(0, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 1, "1", "Up", 0));
            for(int i=0; i<qMin(pinCount, 1); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "battery") {
            def.setCategory("Miscellaneous");
            def.setReferencePrefix("B");
            // Long/short line pairs
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -7.5), QPointF(15, -7.5))); // Long
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, 7.5), QPointF(7.5, 7.5)));  // Short
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -7.5), QPointF(0, -22.5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 7.5), QPointF(0, 22.5)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -22.5), 1, "+", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 22.5), 2, "-", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "relay") {
            def.setCategory("Electromechanical");
            def.setReferencePrefix("RLY");
            // Coil
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-45, -15, 30, 30), false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-30, 15))); // Diagonal through coil
            // Switch
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -15), QPointF(15, -5)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -5), QPointF(30, 10))); // Switch arm
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 15), QPointF(15, 10)));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-30, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 15), QPointF(-30, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, -15), QPointF(15, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 15), QPointF(15, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, -30), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 30), 2, getPinName(1), "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, -30), 3, getPinName(2), "Down", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(15, 30), 4, getPinName(3), "Up", 0));
            for(int i=0; i<qMin(pinCount, 4); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "fuse") {
            def.setCategory("Protective");
            def.setReferencePrefix("F");
            // S-curve for fuse
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-15, -7.5, 15, 15), 0, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(0, -7.5, 15, 15), 180 * 16, 180 * 16));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-30, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(30, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 0), 2, getPinName(1), "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "bridge_rectifier") {
            def.setCategory("Semiconductors");
            def.setReferencePrefix("BR");
            // Diamond body
            QList<QPointF> diamond; diamond << QPointF(0, -30) << QPointF(30, 0) << QPointF(0, 30) << QPointF(-30, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(diamond, false));
            // Internal diodes (abstracted as + and - and ~ labels)
            def.addPrimitive(SymbolPrimitive::createText("+", QPointF(-5, -20), 8));
            def.addPrimitive(SymbolPrimitive::createText("-", QPointF(-5, 10), 8));
            def.addPrimitive(SymbolPrimitive::createText("~", QPointF(15, -5), 8));
            def.addPrimitive(SymbolPrimitive::createText("~", QPointF(-25, -5), 8));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -30), QPointF(0, -45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 30), QPointF(0, 45)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 1, getPinName(0), "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 2, getPinName(1), "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 3, getPinName(2), "Right", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 4, getPinName(3), "Left", 0));
            for(int i=0; i<qMin(pinCount, 4); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "transformer") {
            def.setCategory("Passives");
            def.setReferencePrefix("T");
            // Primary coil (3 arcs)
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-30, -30, 15, 20), 90 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-30, -10, 15, 20), 90 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-30, 10, 15, 20), 90 * 16, 180 * 16));
            // Secondary coil (3 arcs)
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(15, -30, 15, 20), 270 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(15, -10, 15, 20), 270 * 16, 180 * 16));
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(15, 10, 15, 20), 270 * 16, 180 * 16));
            // Core lines
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-5, -25), QPointF(-5, 25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(5, -25), QPointF(5, 25)));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, -30), QPointF(-45, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-22.5, 30), QPointF(-45, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, -30), QPointF(45, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(22.5, 30), QPointF(45, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -30), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 30), 2, getPinName(1), "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -30), 3, getPinName(2), "Left", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 30), 4, getPinName(3), "Left", 0));
            for(int i=0; i<qMin(pinCount, 4); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "potentiometer") {
            def.setCategory("Passives");
            def.setReferencePrefix("RV");
            // Resistor body (zigzag)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-20, -10)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, -10), QPointF(0, 10)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 10), QPointF(20, -10)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(20, -10), QPointF(30, 0)));
            // Wiper
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 10), QPointF(0, 30)));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 2, getPinName(1), "Left", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 3, getPinName(2), "Up", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "crystal") {
            def.setCategory("Passives");
            def.setReferencePrefix("Y");
            // Plates
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, -15), QPointF(-7.5, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, -15), QPointF(7.5, 15)));
            // Crystal block
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-3.75, -11.25, 7.5, 22.5), false));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-7.5, 0), QPointF(-30, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(7.5, 0), QPointF(30, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 0), 2, getPinName(1), "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "comparator") {
            def.setCategory("Comparators");
            def.setReferencePrefix("U");
            QList<QPointF> tri; tri << QPointF(-30, -30) << QPointF(-30, 30) << QPointF(30, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            // Same layout as OpAmp but labeled as comparator
            def.addPrimitive(SymbolPrimitive::createText("CMP", QPointF(-25, -5), 8));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), 1, getPinName(0), "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), 2, getPinName(1), "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 3, getPinName(2), "Down", 0));
            if (pinCount >= 4) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 4, getPinName(3), "Up", 0));
            if (pinCount >= 5) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 5, getPinName(4), "Left", 0));
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "ic8_timer") {
            def.setCategory("Integrated Circuits");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-45, -60,  90, 120), false));
            // 555 Timer Pin Layout (Standard)
            def.addPrimitive(SymbolPrimitive::createText("555", QPointF(-15, -10), 10));
            QMap<int, QString> mapping;
            QStringList timerNames = {"GND", "TRIG", "OUT", "RESET", "CTRL", "THRES", "DISCH", "VCC"};
            for (int i = 0; i < 4; ++i) {
                qreal y = -45 + i * 30;
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-45, y), QPointF(-60, y)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-60, y), i + 1, timerNames.at(i), "Right", 0));
            }
            for (int i = 0; i < 4; ++i) {
                qreal y = 45 - i * 30;
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(45, y), QPointF(60, y)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(60, y), 8 - i, timerNames.at(7 - i), "Left", 0));
            }
            for(int i=0; i<8; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "vref_shunt") {
            def.setCategory("Integrated Circuits");
            def.setReferencePrefix("U");
            // Shunt reference symbol (Zener-like with 3 pins)
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(15, 11.25)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 11.25), QPointF(15, 18.75)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 11.25), QPointF(-15, 3.75)));
            QList<QPointF> tri; tri << QPointF(-15, -11.25) << QPointF(15, -11.25) << QPointF(0, 11.25);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -11.25), QPointF(0, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 11.25), QPointF(0, 30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-30, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -30), 1, "K", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 30), 2, "A", "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 3, "REF", "Right", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "logic_vco") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-40, -30, 80, 60), false));
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 0), 15, false));
            // Sine wave icon
            QPainterPath sine; sine.moveTo(-7.5, 0); sine.quadTo(-3.75, -7.5, 0, 0); sine.quadTo(3.75, 7.5, 7.5, 0);
            for (int i=0; i<sine.toFillPolygon().size()-1; ++i) def.addPrimitive(SymbolPrimitive::createLine(sine.toFillPolygon().at(i), sine.toFillPolygon().at(i+1)));
            def.addPrimitive(SymbolPrimitive::createText("VCO", QPointF(-12, -25), 8));
            
            QMap<int, QString> mapping;
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-40, 0), QPointF(-55, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-55, 0), 1, "IN", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(40, 0), QPointF(55, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(55, 0), 2, "OUT", "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "switch_v" || typeToUse == "switch_i") {
            def.setCategory("Switches");
            def.setReferencePrefix("S");
            // Switch arm
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(11.25, -11.25)));
            // Terminals
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(-15, 0), 1.875, false));
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(15, 0), 1.875, false));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-37.5, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(37.5, 0)));
            if (typeToUse == "switch_i") {
                // Control coil (dashed)
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 15), QPointF(15, 15)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 15), QPointF(-15, 30)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 15), QPointF(15, 30)));
            }
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-37.5, 0), 1, "1", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(37.5, 0), 2, "2", "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "speaker") {
            def.setCategory("Audio");
            def.setReferencePrefix("SPK");
            // Speaker cone
            QList<QPointF> cone; cone << QPointF(-7.5, -7.5) << QPointF(15, -22.5) << QPointF(15, 22.5) << QPointF(-7.5, 7.5);
            def.addPrimitive(SymbolPrimitive::createPolygon(cone, false));
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-15, -7.5, 7.5, 15), false));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, -7.5), QPointF(-11.25, -30)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-11.25, 7.5), QPointF(-11.25, 30)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-11.25, -30), 1, "+", "Down", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-11.25, 30), 2, "-", "Up", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "hall_sensor") {
            def.setCategory("Sensors");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -30, 60, 60), false));
            def.addPrimitive(SymbolPrimitive::createText("HALL", QPointF(-18.75, -7.5), 8));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-45, -15))); // VCC
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 15), QPointF(-45, 15)));  // GND
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));    // OUT
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), 1, "VCC", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), 2, "GND", "Right", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 3, "OUT", "Left", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "gate_and" || typeToUse == "gate_nand") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            const bool nand = (typeToUse == "gate_nand");
            QPainterPath path;
            path.moveTo(-20, -25);
            path.lineTo(0, -25);
            path.arcTo(QRectF(-25, -25, 50, 50), 90, -180);
            path.lineTo(-20, 25);
            path.closeSubpath();
            def.addPrimitive(SymbolPrimitive::createPolygon(path.toFillPolygon().toList(), false));
            if (nand) def.addPrimitive(SymbolPrimitive::createCircle(QPointF(28.75, 0), 3.75, false));
            
            // Input pins
            const int inCount = qMax(2, pinCount - 1);
            for (int i = 0; i < inCount; ++i) {
                qreal y = (inCount == 2) ? (i == 0 ? -12.5 : 12.5) : (-20.0 + i * (40.0 / (inCount - 1)));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, y), QPointF(-40, y)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, y), i + 1, getPinName(i), "Right", 0));
            }
            // Output pin
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(nand ? 32.5 : 25, 0), QPointF(45, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), inCount + 1, getPinName(inCount), "Left", 0));
            
            QMap<int, QString> mapping;
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "gate_or" || typeToUse == "gate_nor" || typeToUse == "gate_xor" || typeToUse == "gate_xnor") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            const bool invert = (typeToUse == "gate_nor" || typeToUse == "gate_xnor");
            const bool xor_gate = (typeToUse == "gate_xor" || typeToUse == "gate_xnor");
            
            QPainterPath path;
            path.moveTo(-15, -25);
            path.quadTo(QPointF(-5, 0), QPointF(-15, 25)); // Back curve
            path.quadTo(QPointF(10, 25), QPointF(30, 0));   // Bottom curve to point
            path.quadTo(QPointF(10, -25), QPointF(-15, -25)); // Top curve back to start
            def.addPrimitive(SymbolPrimitive::createPolygon(path.toFillPolygon().toList(), false));
            
            if (xor_gate) {
                // Second arc for XOR/XNOR - Must be a distinct line
                for (int i = 0; i < 10; ++i) {
                    qreal t1 = i / 10.0;
                    qreal t2 = (i + 1) / 10.0;
                    auto quadPoint = [](qreal t, QPointF p0, QPointF p1, QPointF p2) {
                        return (1-t)*(1-t)*p0 + 2*(1-t)*t*p1 + t*t*p2;
                    };
                    QPointF p0(-22.5, -25), p1(-12.5, 0), p2(-22.5, 25);
                    def.addPrimitive(SymbolPrimitive::createLine(quadPoint(t1, p0, p1, p2), quadPoint(t2, p0, p1, p2)));
                }
            }
            
            if (invert) def.addPrimitive(SymbolPrimitive::createCircle(QPointF(33.75, 0), 3.75, false));
            
            const int inCount = qMax(2, pinCount - 1);
            for (int i = 0; i < inCount; ++i) {
                qreal y = (inCount == 2) ? (i == 0 ? -12.5 : 12.5) : (-20.0 + i * (40.0 / (inCount - 1)));
                // Find X on the back curve for the lead attachment
                // back curve: P0=(-15,-25), P1=(-5,0), P2=(-15,25)
                // y(t) = (1-t)^2*(-25) + 2(1-t)t*0 + t^2*(25) = -25 + 50t => t = (y+25)/50
                qreal t = (y + 25.0) / 50.0;
                qreal x = (1-t)*(1-t)*(-15.0) + 2*(1-t)*t*(-5.0) + t*t*(-15.0);
                
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(x, y), QPointF(-40, y)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-40, y), i + 1, getPinName(i), "Right", 0));
            }
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(invert ? 37.5 : 30, 0), QPointF(45, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), inCount + 1, getPinName(inCount), "Left", 0));
            
            QMap<int, QString> mapping;
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "gate_not" || typeToUse == "gate_buf") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            const bool invert = (typeToUse == "gate_not");
            QList<QPointF> tri; 
            tri << QPointF(-15, -20) << QPointF(-15, 20) << QPointF(15, 0);
            def.addPrimitive(SymbolPrimitive::createPolygon(tri, false));
            if (invert) def.addPrimitive(SymbolPrimitive::createCircle(QPointF(18.75, 0), 3.75, false));
            
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-35, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-35, 0), 1, "A", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(invert ? 22.5 : 15, 0), QPointF(40, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(40, 0), 2, "Y", "Left", 0));
            
            QMap<int, QString> mapping;
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "logic_dff") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -40, 60, 80), false));
            def.addPrimitive(SymbolPrimitive::createText("DFF", QPointF(-12, -5), 10));
            // Clock triangle
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 5), QPointF(-20, 15)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, 15), QPointF(-30, 25)));

            QStringList nodes = sub.pins;
            auto findPin = [&](const QStringList& names, const QString& pattern) -> int {
                QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
                for (int i = 0; i < names.size(); ++i) {
                    if (re.match(names[i]).hasMatch()) return i + 1;
                }
                return -1;
            };

            int idxD = findPin(nodes, "^d$|^data$");
            int idxCLK = findPin(nodes, "clk|clock|cp");
            int idxQ = findPin(nodes, "^q$|^out$");
            int idxQN = findPin(nodes, "qbar|qn|q\\\\");
            int idxPRE = findPin(nodes, "pre|set");
            int idxCLR = findPin(nodes, "clr|clear|res|reset");

            // Fallback to indices if names didn't match (for 74HC74: CLR, D, CLK, PRE, Q, QN)
            if (idxCLR == -1 && nodes.size() >= 1) idxCLR = 1;
            if (idxD == -1 && nodes.size() >= 2) idxD = 2;
            if (idxCLK == -1 && nodes.size() >= 3) idxCLK = 3;
            if (idxPRE == -1 && nodes.size() >= 4) idxPRE = 4;
            if (idxQ == -1 && nodes.size() >= 5) idxQ = 5;
            if (idxQN == -1 && nodes.size() >= 6) idxQN = 6;

            if (idxD != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-45, -15)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), idxD, "D", "Right", 0));
            }
            if (idxCLK != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 15), QPointF(-45, 15)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), idxCLK, "CLK", "Right", 0));
            }
            if (idxQ != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -15), QPointF(45, -15)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -15), idxQ, "Q", "Left", 0));
            }
            if (idxQN != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 15), QPointF(45, 15)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 15), idxQN, "Q\\", "Left", 0));
            }
            
            if (idxPRE != -1) {
                bool activeLow = nodes[idxPRE-1].contains("bar", Qt::CaseInsensitive) || 
                               nodes[idxPRE-1].contains('n', Qt::CaseInsensitive) ||
                               nodes[idxPRE-1].contains('\\');
                if (activeLow) {
                    def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, -41.875), 1.875, false));
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -43.75), QPointF(0, -55)));
                } else {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -40), QPointF(0, -55)));
                }
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -55), idxPRE, "PRE", "Down", 0));
            }
            if (idxCLR != -1) {
                bool activeLow = nodes[idxCLR-1].contains("bar", Qt::CaseInsensitive) || 
                               nodes[idxCLR-1].contains('n', Qt::CaseInsensitive) ||
                               nodes[idxCLR-1].contains('\\');
                if (activeLow) {
                    def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 41.875), 1.875, false));
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 43.75), QPointF(0, 55)));
                } else {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 40), QPointF(0, 55)));
                }
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 55), idxCLR, "CLR", "Up", 0));
            }

            QMap<int, QString> mapping;
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "logic_jkff") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -50, 60, 100), false));
            def.addPrimitive(SymbolPrimitive::createText("JKFF", QPointF(-18, -5), 10));
            // Clock triangle
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -10), QPointF(-20, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, 0), QPointF(-30, 10)));

            QStringList nodes = sub.pins;
            auto findPin = [&](const QStringList& names, const QString& pattern) -> int {
                QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
                for (int i = 0; i < names.size(); ++i) {
                    if (re.match(names[i]).hasMatch()) return i + 1;
                }
                return -1;
            };

            int idxJ = findPin(nodes, "^j$");
            int idxK = findPin(nodes, "^k$");
            int idxCLK = findPin(nodes, "clk|clock|cp");
            int idxQ = findPin(nodes, "^q$");
            int idxQN = findPin(nodes, "qbar|qn|q\\\\");
            int idxPRE = findPin(nodes, "pre|set");
            int idxCLR = findPin(nodes, "clr|clear|res|reset");

            // Fallback for common JKFF order (74LS76: CLK, PRE, CLR, J, K, Q, QN)
            if (idxCLK == -1 && nodes.size() >= 1) idxCLK = 1;
            if (idxPRE == -1 && nodes.size() >= 2) idxPRE = 2;
            if (idxCLR == -1 && nodes.size() >= 3) idxCLR = 3;
            if (idxJ == -1 && nodes.size() >= 4) idxJ = 4;
            if (idxK == -1 && nodes.size() >= 5) idxK = 5;
            if (idxQ == -1 && nodes.size() >= 6) idxQ = 6;
            if (idxQN == -1 && nodes.size() >= 7) idxQN = 7;

            if (idxJ != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -30), QPointF(-45, -30)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -30), idxJ, "J", "Right", 0));
            }
            if (idxCLK != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), idxCLK, "CLK", "Right", 0));
            }
            if (idxK != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 30), QPointF(-45, 30)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 30), idxK, "K", "Right", 0));
            }
            if (idxQ != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, -30), QPointF(45, -30)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, -30), idxQ, "Q", "Left", 0));
            }
            if (idxQN != -1) {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 30), QPointF(45, 30)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 30), idxQN, "Q\\", "Left", 0));
            }

            if (idxPRE != -1) {
                bool activeLow = nodes[idxPRE-1].contains("bar", Qt::CaseInsensitive) || 
                               nodes[idxPRE-1].contains('n', Qt::CaseInsensitive) ||
                               nodes[idxPRE-1].contains('\\');
                if (activeLow) {
                    def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, -51.875), 1.875, false));
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -53.75), QPointF(0, -65)));
                } else {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -50), QPointF(0, -65)));
                }
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -65), idxPRE, "PRE", "Down", 0));
            }
            if (idxCLR != -1) {
                bool activeLow = nodes[idxCLR-1].contains("bar", Qt::CaseInsensitive) || 
                               nodes[idxCLR-1].contains('n', Qt::CaseInsensitive) ||
                               nodes[idxCLR-1].contains('\\');
                if (activeLow) {
                    def.addPrimitive(SymbolPrimitive::createCircle(QPointF(0, 51.875), 1.875, false));
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 53.75), QPointF(0, 65)));
                } else {
                    def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 50), QPointF(0, 65)));
                }
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 65), idxCLR, "CLR", "Up", 0));
            }

            QMap<int, QString> mapping;
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "logic_mux") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            QList<QPointF> muxShape = {QPointF(-20, -40), QPointF(20, -25), QPointF(20, 25), QPointF(-20, 40)};
            def.addPrimitive(SymbolPrimitive::createPolygon(muxShape, false));
            def.addPrimitive(SymbolPrimitive::createText("MUX", QPointF(-10, -5), 8));
            QMap<int, QString> mapping;
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, -20), QPointF(-35, -20)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-35, -20), 1, "I0", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-20, 20), QPointF(-35, 20)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-35, 20), 2, "I1", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 32.5), QPointF(0, 45)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 3, "S", "Up", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(20, 0), QPointF(35, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(35, 0), 4, "Y", "Left", 0));
            for(int i=0; i<qMin(4, pinCount); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "logic_gate_3pin" || typeToUse == "logic_gate_5pin") {
            def.setCategory("Logic");
            def.setReferencePrefix("U");
            // Generic Gate Shape (D-shape)
            QPainterPath path;
            path.moveTo(-15, -30);
            path.lineTo(0, -30);
            path.arcTo(QRectF(-15, -30, 30, 60), 90, -180);
            path.lineTo(-15, 30);
            path.closeSubpath();
            def.addPrimitive(SymbolPrimitive::createPolygon(path.toFillPolygon().toList(), false));
            
            QMap<int, QString> mapping;
            // Inputs
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, -15), QPointF(-30, -15)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, -15), 1, "A", "Right", 0));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 15), QPointF(-30, 15)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 15), 2, "B", "Right", 0));
            // Output
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(30, 0)));
            def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 0), 3, "Y", "Left", 0));
            
            if (typeToUse == "logic_gate_5pin") {
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, -30), QPointF(0, -45)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, -45), 4, "VCC", "Down", 0));
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 30), QPointF(0, 45)));
                def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 5, "GND", "Up", 0));
            }
            for(int i=0; i<pinCount; ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "regulator") {
            def.setCategory("Power");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -30, 60, 60), false));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 0), QPointF(-45, 0))); // IN
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));  // OUT
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(0, 30), QPointF(0, 45)));   // GND
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 0), 1, "IN", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(0, 45), 2, "GND", "Up", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(45, 0), 3, "OUT", "Left", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "supervisor") {
            def.setCategory("Power");
            def.setReferencePrefix("U");
            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-30, -30, 60, 60), false));
            def.addPrimitive(SymbolPrimitive::createText("RESET", QPointF(-20, -10), 8));
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, -15), QPointF(-45, -15))); // VCC
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-30, 15), QPointF(-45, 15)));   // GND
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(30, 0), QPointF(45, 0)));    // RESET
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, -15), 1, "VCC", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 0), 2, "RST", "Left", 0));
            if (pinCount >= 3) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-45, 15), 3, "GND", "Right", 0));
            for(int i=0; i<qMin(pinCount, 3); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else if (typeToUse == "switch_v") {
            def.setCategory("Switches");
            def.setReferencePrefix("S");
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(-15, 0), 3.75, false));
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(15, 0), 3.75, false));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(15, -15))); // Arm
            // Leads
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(-15, 0), QPointF(-30, 0)));
            def.addPrimitive(SymbolPrimitive::createLine(QPointF(15, 0), QPointF(30, 0)));
            QMap<int, QString> mapping;
            if (pinCount >= 1) def.addPrimitive(SymbolPrimitive::createPin(QPointF(-30, 0), 1, "P1", "Right", 0));
            if (pinCount >= 2) def.addPrimitive(SymbolPrimitive::createPin(QPointF(30, 0), 2, "P2", "Left", 0));
            for(int i=0; i<qMin(pinCount, 2); ++i) mapping.insert(i+1, getPinName(i));
            def.setSpiceNodeMapping(mapping);

        } else {
            // Default IC Shape (Rectangle with Dot and Notch)
            const int leftCount = (pinCount + 1) / 2;
            const qreal bodyHeight = leftCount * pinSpacing;
            const qreal halfH = bodyHeight / 2.0;
            const qreal halfW = bodyWidth / 2.0;

            def.addPrimitive(SymbolPrimitive::createRect(QRectF(-halfW, -halfH, bodyWidth, bodyHeight), false));
            
            // Top notch
            def.addPrimitive(SymbolPrimitive::createArc(QRectF(-11.25, -halfH - 5.625, 22.5, 11.25), 0, 180 * 16));
            
            // Pin 1 indicator dot
            def.addPrimitive(SymbolPrimitive::createCircle(QPointF(-halfW + 11.25, -halfH + 11.25), 3.75, true));
            
            def.addPrimitive(SymbolPrimitive::createText(sub.name, QPointF(-halfW + 5, -halfH - 20.0), 10));

            QMap<int, QString> mapping;
            // Left side pins
            for (int i = 0; i < leftCount; ++i) {
                const qreal y = -halfH + 15.0 + i * pinSpacing;
                const QPointF pos(-halfW - pinLength, y);
                // Pin lead
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(-halfW, y), QPointF(-halfW - pinLength, y)));
                // Pin
                def.addPrimitive(SymbolPrimitive::createPin(pos, i + 1, getPinName(i), "Right", 0));
                mapping.insert(i + 1, getPinName(i));
            }
            // Right side pins (bottom-up)
            const int rightCount = pinCount - leftCount;
            for (int i = 0; i < rightCount; ++i) {
                const qreal y = halfH - 15.0 - i * pinSpacing;
                const QPointF pos(halfW + pinLength, y);
                // Pin lead
                def.addPrimitive(SymbolPrimitive::createLine(QPointF(halfW, y), QPointF(halfW + pinLength, y)));
                // Pin
                def.addPrimitive(SymbolPrimitive::createPin(pos, leftCount + i + 1, getPinName(leftCount + i), "Left", 0));
                mapping.insert(leftCount + i + 1, getPinName(leftCount + i));
            }
            def.setSpiceNodeMapping(mapping);
        }

        QString outPath = QDir(outDir).filePath(sub.name.toLower() + ".viosym");
        QFile outFile(outPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(def.toJson());
            outFile.write(doc.toJson(QJsonDocument::Indented));
            outFile.close();
            if (!g_quiet) std::cout << "Generated symbol: " << outPath.toStdString() << " (from " << QFileInfo(inputPath).fileName().toStdString() << ")" << std::endl;
            count++;
        }
    }
    return count;
}

bool runSymbolFromSubckt(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 3) {
        std::cerr << "Usage: viora symbol-from-subckt <input.cir|lib> <out_dir> [--name <subckt>] [--type ic|op|...]" << std::endl;
        return false;
    }
    const QString inputPath = args.at(1);
    const QString outDir = args.at(2);
    const QString targetName = parser.value("name");
    const QString symbolType = parser.value("symbol-type").toLower();

    int count = generateSymbolsForLibrary(inputPath, outDir, symbolType, targetName);

    if (parser.isSet("json")) {
        QJsonObject res;
        res["ok"] = true;
        res["count"] = count;
        printJsonValue(res);
    }

    return count > 0;
}

bool runLibraryToSymbols(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 3) {
        std::cerr << "Usage: viora library-to-symbols <input_path> <out_dir> [--type ic|op|...] [--recursive]" << std::endl;
        return false;
    }
    QString inputPath = args.at(1);
    const QString outBaseDir = args.at(2);
    const QString symbolType = parser.value("symbol-type").toLower();
    const bool recursive = parser.isSet("recursive");

    if (inputPath.startsWith("~")) {
        inputPath.replace(0, 1, QDir::homePath());
    }

    QString rootToUse;
    QStringList filesToProcess;
    QFileInfo inInfo(inputPath);
    
    if (inInfo.isDir()) {
        rootToUse = inInfo.absoluteFilePath();
        QDirIterator it(rootToUse, {"*.lib", "*.sub", "*.spi", "*.mod", "*.cir"}, 
                        QDir::Files, recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            filesToProcess << it.next();
        }
    } else if (inInfo.exists()) {
        rootToUse = inInfo.absolutePath();
        filesToProcess << inputPath;
    } else {
        std::cerr << "Error: Input path not found: " << inputPath.toStdString() << std::endl;
        return false;
    }

    if (filesToProcess.isEmpty()) {
        std::cerr << "No SPICE library files found to process." << std::endl;
        return false;
    }

    int totalSymbols = 0;
    int filesProcessed = 0;

    for (const QString& filePath : filesToProcess) {
        QFileInfo fi(filePath);
        // Calculate relative path from root to preserve subfolder structure
        QString relativePath = QDir(rootToUse).relativeFilePath(fi.absolutePath());
        QString libName = fi.completeBaseName();
        
        // Build target directory: out_dir / relative_subfolder / lib_name
        QDir targetDirObj(outBaseDir);
        if (relativePath != ".") {
            targetDirObj.setPath(targetDirObj.filePath(relativePath));
        }
        QString targetDir = targetDirObj.filePath(libName);
        
        int count = generateSymbolsForLibrary(filePath, targetDir, symbolType);
        if (count > 0) {
            totalSymbols += count;
            filesProcessed++;
        }
    }

    if (!g_quiet) {
        std::cout << "Successfully processed " << filesProcessed << " library files." << std::endl;
        std::cout << "Generated total of " << totalSymbols << " symbols in " << outBaseDir.toStdString() << std::endl;
    }

    if (parser.isSet("json")) {
        QJsonObject res;
        res["ok"] = true;
        res["files_processed"] = filesProcessed;
        res["total_symbols"] = totalSymbols;
        printJsonValue(res);
    }

    return filesProcessed > 0;
}

bool runLibraryAutoConvert(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 3) {
        std::cerr << "Usage: viora library-auto-convert <input_path> <out_dir> [--mapping <mapping.json>] [--recursive]" << std::endl;
        return false;
    }
    QString inputPath = args.at(1);
    const QString outBaseDir = args.at(2);
    const QString mappingPath = parser.value("mapping");
    const bool recursive = parser.isSet("recursive");

    SymbolMatcher matcher;
    if (!mappingPath.isEmpty()) {
        if (!matcher.loadMapping(mappingPath)) {
            std::cerr << "Error: Failed to load mapping JSON: " << mappingPath.toStdString() << std::endl;
            return false;
        }
    }

    if (inputPath.startsWith("~")) {
        inputPath.replace(0, 1, QDir::homePath());
    }

    QString rootToUse;
    QStringList filesToProcess;
    QFileInfo inInfo(inputPath);
    
    if (inInfo.isDir()) {
        rootToUse = inInfo.absoluteFilePath();
        QDirIterator it(rootToUse, {"*.lib", "*.sub", "*.spi", "*.mod", "*.cir"}, 
                        QDir::Files, recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            filesToProcess << it.next();
        }
    } else if (inInfo.exists()) {
        rootToUse = inInfo.absolutePath();
        filesToProcess << inputPath;
    } else {
        std::cerr << "Error: Input path not found: " << inputPath.toStdString() << std::endl;
        return false;
    }

    if (filesToProcess.isEmpty()) {
        std::cerr << "No SPICE library files found to process." << std::endl;
        return false;
    }

    int totalSymbols = 0;
    int filesProcessed = 0;

    for (const QString& filePath : filesToProcess) {
        QFileInfo fi(filePath);
        QString relativePath = QDir(rootToUse).relativeFilePath(fi.absolutePath());
        QString libName = fi.completeBaseName();
        
        QDir targetDirObj(outBaseDir);
        if (relativePath != ".") {
            targetDirObj.setPath(targetDirObj.filePath(relativePath));
        }
        QString targetDir = targetDirObj.filePath(libName);
        
        int count = generateSymbolsForLibrary(filePath, targetDir, "", "", &matcher);
        if (count > 0) {
            totalSymbols += count;
            filesProcessed++;
        }
    }

    if (!g_quiet) {
        std::cout << "Successfully auto-converted " << filesProcessed << " library files." << std::endl;
        std::cout << "Generated total of " << totalSymbols << " symbols in " << outBaseDir.toStdString() << std::endl;
    }

    return filesProcessed > 0;
}

// ============================================================================
// WebSocket client for UICommandServer communication
// ============================================================================
static bool sendWebSocketCommand(const QString& host, int port, const QVariantMap& cmd, QVariantMap& response) {
    QTcpSocket socket;
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(3000)) {
        return false;
    }

    QByteArray payload = QJsonDocument::fromVariant(cmd).toJson(QJsonDocument::Compact);

    // --- WebSocket handshake (RFC 6455) ---
    QByteArray key(16, 0);
    for (int i = 0; i < 16; ++i)
        key[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
    QByteArray keyBase64 = key.toBase64();

    QByteArray hostBytes = host.toUtf8();
    QByteArray request;
    request.append("GET / HTTP/1.1\r\n");
    request.append("Host: " + hostBytes + ":" + QByteArray::number(port) + "\r\n");
    request.append("Upgrade: websocket\r\n");
    request.append("Connection: Upgrade\r\n");
    request.append("Sec-WebSocket-Key: " + keyBase64 + "\r\n");
    request.append("Sec-WebSocket-Version: 13\r\n");
    request.append("\r\n");

    socket.write(request);
    socket.waitForBytesWritten(1000);

    if (!socket.waitForReadyRead(5000)) {
        return false;
    }

    QByteArray handshakeResponse = socket.readAll();
    if (!handshakeResponse.contains("101")) {
        return false;
    }

    // --- Build masked WebSocket frame (client must mask) ---
    QByteArray frame;
    frame.append(static_cast<char>(0x81)); // FIN + TEXT opcode

    int len = payload.size();
    if (len <= 125) {
        frame.append(static_cast<char>(0x80 | len));
    } else if (len <= 65535) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.append(static_cast<char>((len >> (8 * i)) & 0xFF));
    }

    QByteArray maskKey(4, 0);
    for (int i = 0; i < 4; ++i)
        maskKey[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
    frame.append(maskKey);

    QByteArray maskedPayload = payload;
    for (int i = 0; i < maskedPayload.size(); ++i)
        maskedPayload[i] ^= maskKey[i % 4];
    frame.append(maskedPayload);

    socket.write(frame);
    socket.waitForBytesWritten(1000);

    // --- Read and parse response frame ---
    if (!socket.waitForReadyRead(5000)) {
        return false;
    }

    QByteArray rawData = socket.readAll();
    if (rawData.size() < 2) return false;

    int opcode = rawData[0] & 0x0F;
    if (opcode == 0x08) return false; // Connection close

    quint64 payloadLen = rawData[1] & 0x7F;
    int offset = 2;

    if (payloadLen == 126) {
        if (rawData.size() < 4) return false;
        payloadLen = (static_cast<quint8>(rawData[2]) << 8) | static_cast<quint8>(rawData[3]);
        offset = 4;
    } else if (payloadLen == 127) {
        if (rawData.size() < 10) return false;
        payloadLen = 0;
        for (int i = 0; i < 8; ++i)
            payloadLen = (payloadLen << 8) | static_cast<quint8>(rawData[2 + i]);
        offset = 10;
    }

    if (rawData.size() < offset + payloadLen) return false;
    QByteArray responseData = rawData.mid(offset, payloadLen);

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    response = doc.toVariant().toMap();
    return true;
}

// ============================================================================
// GUI remote control: interact with running GUI widgets
// ============================================================================
static bool sendGuiCommand(const QVariantMap& cmd, QVariantMap& response) {
    return sendWebSocketCommand("127.0.0.1", 18790, cmd, response);
}

bool runGui(const QStringList& rawArgs, const QCommandLineParser& parser) {
    if (rawArgs.size() < 3) {
        std::cerr << "Usage: viora gui <subcommand> [options]\n";
        std::cerr << "\nSubcommands:\n";
        std::cerr << "  list-buttons   List interactive elements in a window\n";
        std::cerr << "  click <target> Click a button or trigger an action\n";
        std::cerr << "  type <field> <text>  Type text into an input field\n";
        std::cerr << "  menu <action>  Trigger a menu action\n";
        return false;
    }

    QString subcmd = rawArgs.at(2);
    bool jsonOutput = parser.isSet("json");

    // --- Parse --window option ---
    QString window;
    for (int i = 3; i < rawArgs.size(); ++i) {
        if (rawArgs.at(i) == "--window" && i + 1 < rawArgs.size()) {
            window = rawArgs.at(++i);
        }
    }
    if (window.isEmpty()) {
        // Default to first visible top-level window
        window = "SchematicEditor";
    }

    // --- list-buttons ---
    if (subcmd == "list-buttons") {
        QString filterType;
        QString filterParent;
        for (int i = 3; i < rawArgs.size(); ++i) {
            if (rawArgs.at(i) == "--type" && i + 1 < rawArgs.size())
                filterType = rawArgs.at(++i);
            else if (rawArgs.at(i) == "--parent" && i + 1 < rawArgs.size())
                filterParent = rawArgs.at(++i);
            else if (rawArgs.at(i) == "--window" && i + 1 < rawArgs.size())
                ++i;
        }

        QVariantMap cmd;
        cmd["cmd"] = "gui_list_elements";
        QVariantMap params;
        params["window"] = window;
        if (!filterType.isEmpty()) params["type"] = filterType;
        if (!filterParent.isEmpty()) params["parent"] = filterParent;
        cmd["params"] = params;

        QVariantMap response;
        if (!sendGuiCommand(cmd, response)) {
            std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
            return false;
        }

        if (jsonOutput) {
            std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            return true;
        }

        QJsonArray elements = response["elements"].toJsonArray();
        if (elements.isEmpty()) {
            std::cout << "No interactive elements found in " << window.toStdString() << std::endl;
            return true;
        }

        std::cout << "Interactive elements in " << window.toStdString() << ":" << std::endl;
        for (const auto& e : elements) {
            QJsonObject obj = e.toObject();
            std::cout << "  [" << obj["type"].toString().toStdString() << "] "
                      << obj["label"].toString().toStdString();
            if (!obj["objectName"].toString().isEmpty())
                std::cout << " (" << obj["objectName"].toString().toStdString() << ")";
            if (!obj["parentName"].toString().isEmpty())
                std::cout << " in " << obj["parentName"].toString().toStdString();
            std::cout << std::endl;
        }
        return true;
    }

    // --- click ---
    if (subcmd == "click") {
        if (rawArgs.size() < 4) {
            std::cerr << "Usage: viora gui click <label-or-name> [--window <name>]" << std::endl;
            return false;
        }
        QString target = rawArgs.at(3);

        QVariantMap cmd;
        cmd["cmd"] = "gui_click";
        QVariantMap params;
        params["window"] = window;
        params["target"] = target;
        cmd["params"] = params;

        QVariantMap response;
        if (!sendGuiCommand(cmd, response)) {
            std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
            return false;
        }

        if (jsonOutput) {
            std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            return response.value("ok").toBool();
        }

        if (!response.value("ok").toBool()) {
            std::cerr << "Error: " << response.value("error").toString().toStdString() << std::endl;
            return false;
        }

        std::cout << "Clicked: " << response.value("label").toString().toStdString()
                  << " (" << response.value("type").toString().toStdString() << ")" << std::endl;
        return true;
    }

    // --- type ---
    if (subcmd == "type") {
        if (rawArgs.size() < 5) {
            std::cerr << "Usage: viora gui type <field-name> <text> [--window <name>] [--append]" << std::endl;
            return false;
        }
        QString fieldName = rawArgs.at(3);
        QString text = rawArgs.at(4);
        bool append = false;
        for (int i = 5; i < rawArgs.size(); ++i) {
            if (rawArgs.at(i) == "--append") append = true;
        }

        QVariantMap cmd;
        cmd["cmd"] = "gui_type";
        QVariantMap params;
        params["window"] = window;
        params["target"] = fieldName;
        params["text"] = text;
        params["append"] = append;
        cmd["params"] = params;

        QVariantMap response;
        if (!sendGuiCommand(cmd, response)) {
            std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
            return false;
        }

        if (jsonOutput) {
            std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            return response.value("ok").toBool();
        }

        if (!response.value("ok").toBool()) {
            std::cerr << "Error: " << response.value("error").toString().toStdString() << std::endl;
            return false;
        }

        std::cout << "Typed into: " << response.value("field").toString().toStdString() << std::endl;
        return true;
    }

    // --- menu ---
    if (subcmd == "menu") {
        if (rawArgs.size() < 4) {
            std::cerr << "Usage: viora gui menu <action-text> [--window <name>]" << std::endl;
            return false;
        }
        QString actionText = rawArgs.at(3);

        QVariantMap cmd;
        cmd["cmd"] = "gui_menu";
        QVariantMap params;
        params["window"] = window;
        params["action"] = actionText;
        cmd["params"] = params;

        QVariantMap response;
        if (!sendGuiCommand(cmd, response)) {
            std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
            return false;
        }

        if (jsonOutput) {
            std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            return response.value("ok").toBool();
        }

        if (!response.value("ok").toBool()) {
            std::cerr << "Error: " << response.value("error").toString().toStdString() << std::endl;
            return false;
        }

        std::cout << "Triggered: " << response.value("action").toString().toStdString() << std::endl;
        return true;
    }

    std::cerr << "Unknown gui subcommand: " << subcmd.toStdString() << std::endl;
    std::cerr << "Available subcommands: list-buttons, click, type, menu" << std::endl;
    return false;
}

// ============================================================================
// Screenshot command: captures any widget via UICommandServer
// ============================================================================
bool runScreenshot(const QStringList& rawArgs, const QCommandLineParser& parser) {
    // --- Parse CLI arguments ---
    QString name;
    QString output;
    qreal scale = 1.0;
    bool clipboard = false;
    bool jsonOutput = parser.isSet("json");
    bool includeHidden = false;
    bool listChildren = false;
    QString format = "PNG";
    QRect region;
    bool watchMode = false;
    int interval = 1000;
    QString outputDir;

    for (int i = 2; i < rawArgs.size(); ++i) {
        const QString& arg = rawArgs.at(i);
        if (arg == "--name" && i + 1 < rawArgs.size()) {
            name = rawArgs.at(++i);
        } else if (arg == "--output" && i + 1 < rawArgs.size()) {
            output = rawArgs.at(++i);
        } else if (arg == "--scale" && i + 1 < rawArgs.size()) {
            scale = rawArgs.at(++i).toDouble();
        } else if (arg == "--format" && i + 1 < rawArgs.size()) {
            format = rawArgs.at(++i).toUpper();
        } else if (arg == "--region" && i + 1 < rawArgs.size()) {
            QStringList parts = rawArgs.at(++i).split(",");
            if (parts.size() == 4)
                region = QRect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), parts[3].toInt());
        } else if (arg == "--interval" && i + 1 < rawArgs.size()) {
            interval = rawArgs.at(++i).toInt();
        } else if (arg == "--output-dir" && i + 1 < rawArgs.size()) {
            outputDir = rawArgs.at(++i);
        } else if (arg == "--clipboard") {
            clipboard = true;
        } else if (arg == "--include-hidden") {
            includeHidden = true;
        } else if (arg == "--list-children") {
            listChildren = true;
        } else if (arg == "--watch") {
            watchMode = true;
        }
    }

    // --- List children of a parent widget ---
    if (listChildren) {
        if (name.isEmpty()) {
            std::cerr << "Error: --list-children requires --name <parent>" << std::endl;
            return false;
        }
        QVariantMap cmd;
        cmd["cmd"] = "screenshot_children";
        QVariantMap params;
        params["parent"] = name;
        cmd["params"] = params;
        QVariantMap response;
        if (!sendWebSocketCommand("127.0.0.1", 18790, cmd, response)) {
            std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
            return false;
        }
        if (jsonOutput) {
            std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            return true;
        }
        QJsonArray children = response["children"].toJsonArray();
        if (children.isEmpty()) {
            std::cout << "No children found for: " << name.toStdString() << std::endl;
            return true;
        }
        std::cout << "Children of " << name.toStdString() << ":" << std::endl;
        for (const auto& c : children) {
            std::cout << "  - " << c.toString().toStdString() << std::endl;
        }
        return true;
    }

    // --- List all visible windows ---
    if (name.isEmpty() && !watchMode) {
        QVariantMap cmd;
        cmd["cmd"] = "screenshot_list";
        QVariantMap params;
        params["include_hidden"] = includeHidden;
        cmd["params"] = params;
        QVariantMap response;
        if (!sendWebSocketCommand("127.0.0.1", 18790, cmd, response)) {
            std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
            std::cerr << "Make sure VioSpice GUI is running." << std::endl;
            return false;
        }
        if (jsonOutput) {
            std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            return true;
        }
        QJsonArray windows = response["windows"].toJsonArray();
        if (windows.isEmpty()) {
            std::cout << "No visible windows found." << std::endl;
            return true;
        }
        std::cout << "Available windows:" << std::endl;
        for (const auto& w : windows) {
            QJsonObject obj = w.toObject();
            std::cout << "  [" << obj["index"].toInt() << "] "
                      << obj["class"].toString().toStdString()
                      << " - " << obj["title"].toString().toStdString()
                      << std::endl;
            if (obj.contains("children")) {
                QJsonArray children = obj["children"].toArray();
                for (const auto& c : children) {
                    std::cout << "      " << c.toString().toStdString() << std::endl;
                }
            }
        }
        return true;
    }

    // --- Single capture lambda (used by both normal and watch mode) ---
    auto captureOnce = [&]() -> bool {
        QVariantMap params;
        params["name"] = name;
        params["clipboard"] = clipboard;
        params["scale"] = scale;
        params["format"] = format;
        params["include_hidden"] = includeHidden;
        if (!output.isEmpty()) {
            params["output"] = output;
        }
        if (!region.isNull()) {
            QVariantList r;
            r << region.x() << region.y() << region.width() << region.height();
            params["region"] = r;
        }

        QVariantMap cmd;
        cmd["cmd"] = "screenshot_capture";
        cmd["params"] = params;

        QVariantMap response;
        if (!sendWebSocketCommand("127.0.0.1", 18790, cmd, response)) {
            std::cerr << "Error: Cannot connect to running VioSpice instance (port 18790)" << std::endl;
            std::cerr << "Make sure VioSpice GUI is running." << std::endl;
            return false;
        }

        if (jsonOutput) {
            std::cout << QJsonDocument::fromVariant(response).toJson(QJsonDocument::Compact).toStdString() << std::endl;
            return response.value("ok").toBool();
        }

        if (!response.value("ok").toBool()) {
            std::cerr << "Error: " << response.value("error").toString().toStdString() << std::endl;
            return false;
        }

        std::cout << "Screenshot captured: "
                  << response.value("width").toInt() << "x"
                  << response.value("height").toInt() << std::endl;
        if (response.contains("path")) {
            std::cout << "Saved to: " << response.value("path").toString().toStdString() << std::endl;
        }
        if (clipboard) {
            std::cout << "Copied to clipboard." << std::endl;
        }
        return true;
    };

    // --- Watch mode: continuous frame capture ---
    if (watchMode) {
        if (name.isEmpty()) {
            std::cerr << "Error: --watch requires --name <window>" << std::endl;
            return false;
        }
        if (outputDir.isEmpty())
            outputDir = ".";
        std::cout << "Watching " << name.toStdString() << " every " << interval << "ms. Press Ctrl+C to stop." << std::endl;

        QElapsedTimer timer;
        timer.start();
        int frame = 0;

        while (true) {
            if (timer.elapsed() >= interval) {
                QString origOutput = output;
                output = QString("%1/%2_frame_%3.%4")
                    .arg(outputDir)
                    .arg(name)
                    .arg(frame++, 4, 10, QChar('0'))
                    .arg(format.toLower());
                captureOnce();
                output = origOutput;
                timer.restart();
            }
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
    }

    return captureOnce();
}

bool runItemRender(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 3) {
        std::cerr << "Usage: viora item-render <file.json> <out.png> [--transparent] [--scale <n>]" << std::endl;
        return false;
    }
    const QString filePath = args.at(1);
    const QString outPath = args.at(2);
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read item JSON file: " << filePath.toStdString() << std::endl;
        return false;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Error: Invalid item JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }
    
    QJsonObject itemJson = doc.object();
    QString type = itemJson.value("type").toString();
    if (type.isEmpty()) {
        std::cerr << "Error: JSON missing 'type' field." << std::endl;
        return false;
    }
    
    QGraphicsScene scene;
    SchematicItem* item = SchematicItemFactory::instance().createItem(type, QPointF(0, 0), itemJson, nullptr);
    if (!item) {
        std::cerr << "Error: Failed to create item of type: " << type.toStdString() << std::endl;
        return false;
    }
    scene.addItem(item);
    
    // Ensure styles/fonts are initialized for the item
    if (auto* term = dynamic_cast<VirtualTerminalItem*>(item)) {
        // Force update or apply styles if necessary for specific items
    }
    
    QRectF rect = item->boundingRect();
    if (rect.isNull() || rect.width() <= 0 || rect.height() <= 0) {
        rect = QRectF(-20, -20, 40, 40); // fallback
    }

    const qreal margin = 10.0;
    const bool transparent = parser.isSet("transparent");
    const qreal scale = qMax(0.1, parser.value("scale").toDouble());
    
    QSize imageSize = QSize(qCeil((rect.width() + margin * 2.0) * scale),
                            qCeil((rect.height() + margin * 2.0) * scale));

    QImage image(imageSize, QImage::Format_ARGB32);
    image.fill(transparent ? Qt::transparent : QColor(30, 30, 30));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    
    // Setup rendering context for the scene
    painter.translate((-rect.left() + margin) * scale, (-rect.top() + margin) * scale);
    painter.scale(scale, scale);

    QStyleOptionGraphicsItem opt;
    item->paint(&painter, &opt, nullptr);
    painter.end();
    
    if (!image.save(outPath)) {
        std::cerr << "Error: Failed to save rendered item to " << outPath.toStdString() << std::endl;
        return false;
    }

    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = filePath;
        out["output"] = outPath;
        out["transparent"] = transparent;
        out["scale"] = scale;
        out["type"] = type;
        printJsonValue(out);
    } else {
        printInfoStd("Rendered item to " + outPath.toStdString());
    }
    return true;
}

bool runSymbolRender(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 3) {
        std::cerr << "Usage: viora symbol-render <file.viosym> <out.png>" << std::endl;
        return false;
    }
    const QString filePath = args.at(1);
    const QString outPath = args.at(2);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read symbol file: " << filePath.toStdString() << std::endl;
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Error: Invalid symbol JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }

    QJsonObject obj = doc.object();
    if (obj.contains("library")) {
        std::cerr << "Error: This looks like a library file (.sclib), not a .viosym." << std::endl;
        return false;
    }

    SymbolDefinition symbol = SymbolDefinition::fromJson(obj);
    if (symbol.name().trimmed().isEmpty()) {
        symbol.setName(QFileInfo(filePath).completeBaseName());
    }

    const bool transparent = parser.isSet("transparent");
    const qreal scale = qMax(0.1, parser.value("scale").toDouble());
    if (!renderSymbolToPng(symbol, outPath, transparent, scale)) {
        std::cerr << "Error: Failed to render symbol to " << outPath.toStdString() << std::endl;
        return false;
    }
    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = filePath;
        out["output"] = outPath;
        out["transparent"] = transparent;
        out["scale"] = scale;
        printJsonValue(out);
    } else {
    printInfoStd("Rendered symbol to " + outPath.toStdString());
    }
    return true;
}

bool runSymbolSearch(const QStringList& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: viora symbol-search <query>" << std::endl;
        return false;
    }
    const QString query = args.at(1);
    
    QList<SymbolLibrary::SymbolInfo> results = SymbolLibraryManager::instance().searchMetadata(query);
    
    QJsonObject out;
    out["query"] = query;
    out["count"] = results.size();
    QJsonArray items;
    for (const auto& info : results) {
        QJsonObject item;
        item["name"] = info.name;
        item["library"] = info.library;
        item["category"] = info.category;
        item["description"] = info.description;
        item["tags"] = info.tags;
        items.append(item);
    }
    out["results"] = items;
    printJsonValue(out);
    return true;
}

bool runSymbolList(const QStringList& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: viora symbol-list <folder|library.sclib>" << std::endl;
        return false;
    }
    const QString path = args.at(1);
    QFileInfo info(path);
    if (!info.exists()) {
        std::cerr << "Error: Path not found: " << path.toStdString() << std::endl;
        return false;
    }

    QJsonObject out;
    out["path"] = path;
    QJsonArray symbols;

    auto appendSymbol = [&](const QString& name, const QString& source) {
        QJsonObject s;
        s["name"] = name;
        s["source"] = source;
        symbols.append(s);
    };

    if (info.isFile() && path.endsWith(".sclib", Qt::CaseInsensitive)) {
        SymbolLibrary lib;
        if (!lib.load(path)) {
            std::cerr << "Error: Failed to load library: " << path.toStdString() << std::endl;
            return false;
        }
        for (const QString& name : lib.symbolNames()) {
            appendSymbol(name, QFileInfo(path).fileName());
        }
    } else if (info.isDir()) {
        SymbolLibraryManager& manager = SymbolLibraryManager::instance();
        manager.loadUserLibraries(path, false); 
        
        for (SymbolLibrary* lib : manager.libraries()) {
            if (lib->path().startsWith(path)) {
                const QList<SymbolLibrary::SymbolInfo> infos = lib->symbolInfos();
                for (const auto& i : infos) {
                    QJsonObject s;
                    s["name"] = i.name;
                    s["source"] = lib->name();
                    s["category"] = i.category;
                    s["description"] = i.description;
                    symbols.append(s);
                }
            }
        }
    } else {
        std::cerr << "Error: Unsupported path. Use a folder or .sclib file." << std::endl;
        return false;
    }

    out["symbols"] = symbols;
    printJsonValue(out);
    return true;
}

bool runSymbolExport(const QStringList& args) {
    if (args.size() < 4) {
        std::cerr << "Usage: viora symbol-export <symbolName> <library.sclib> <out.viosym>" << std::endl;
        return false;
    }
    const QString symName = args.at(1);
    const QString libPath = args.at(2);
    const QString outPath = args.at(3);

    if (!QFileInfo::exists(libPath)) {
        std::cerr << "Error: Library not found: " << libPath.toStdString() << std::endl;
        return false;
    }

    SymbolLibrary lib;
    if (!lib.load(libPath)) {
        std::cerr << "Error: Failed to load library: " << libPath.toStdString() << std::endl;
        return false;
    }

    SymbolDefinition* sym = lib.findSymbol(symName);
    if (!sym) {
        std::cerr << "Error: Symbol not found: " << symName.toStdString() << std::endl;
        return false;
    }

    QString finalOut = outPath;
    if (!finalOut.endsWith(".viosym", Qt::CaseInsensitive)) finalOut += ".viosym";

    QJsonDocument doc(sortJsonValue(sym->toJson()).toObject());
    QFile file(finalOut);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "Error: Cannot write " << finalOut.toStdString() << std::endl;
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    printInfoStd("Exported " + symName.toStdString() + " to " + finalOut.toStdString());
    return true;
}

bool runSymbolImport(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 3) {
        std::cerr << "Usage: viora symbol-import <input.asy|input.kicad_sym> <out.viosym|out.sclib> [--name SYMBOL]" << std::endl;
        return false;
    }

    const QString inPath = args.at(1);
    const QString outPath = args.at(2);
    const QFileInfo inInfo(inPath);
    if (!inInfo.exists()) {
        std::cerr << "Error: Input not found: " << inPath.toStdString() << std::endl;
        return false;
    }

    const QString lowerIn = inPath.toLower();
    SymbolDefinition symbol;
    QString detectedFootprint;

    if (lowerIn.endsWith(".asy")) {
        auto result = LtspiceSymbolImporter::importSymbolDetailed(inPath);
        if (!result.success || !result.symbol.isValid()) {
            const QString msg = result.errorMessage.isEmpty()
                                    ? "Failed to import LTspice symbol."
                                    : result.errorMessage;
            std::cerr << "Error: " << msg.toStdString() << std::endl;
            return false;
        }
        symbol = result.symbol;
    } else if (lowerIn.endsWith(".kicad_sym")) {
        const QString symName = parser.value("symbol-name").trimmed();
        if (symName.isEmpty()) {
            QStringList names = KicadSymbolImporter::getSymbolNames(inPath);
            names.sort(Qt::CaseInsensitive);

            QJsonObject out;
            out["file"] = inPath;
            QJsonArray list;
            for (const QString& n : names) list.append(n);
            out["symbols"] = list;
            printJsonValue(out);
            return true;
        }
        auto result = KicadSymbolImporter::importSymbolDetailed(inPath, symName);
        symbol = result.symbol;
        detectedFootprint = result.detectedFootprint;
        if (!symbol.isValid()) {
            std::cerr << "Error: Failed to import KiCad symbol: " << symName.toStdString() << std::endl;
            return false;
        }
    } else {
        std::cerr << "Error: Unsupported input. Use .asy or .kicad_sym" << std::endl;
        return false;
    }

    normalizeSymbolToStandardSize(symbol);

    QString finalOut = outPath;
    const QString lowerOut = outPath.toLower();
    if (lowerOut.endsWith(".viosym")) {
        QJsonDocument doc(sortJsonValue(symbol.toJson()).toObject());
        QFile file(finalOut);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::cerr << "Error: Cannot write " << finalOut.toStdString() << std::endl;
            return false;
        }
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    } else if (lowerOut.endsWith(".sclib")) {
        SymbolLibrary lib;
        if (QFileInfo::exists(finalOut)) {
            if (!lib.load(finalOut)) {
                std::cerr << "Error: Failed to load library: " << finalOut.toStdString() << std::endl;
                return false;
            }
        } else {
            lib.setName(QFileInfo(finalOut).completeBaseName());
            lib.setBuiltIn(false);
            lib.setPath(finalOut);
        }
        lib.addSymbol(symbol);
        if (!lib.save(finalOut)) {
            std::cerr << "Error: Failed to save library: " << finalOut.toStdString() << std::endl;
            return false;
        }
    } else {
        std::cerr << "Error: Output must be .viosym or .sclib" << std::endl;
        return false;
    }

    QJsonObject out;
    out["input"] = inPath;
    out["output"] = finalOut;
    out["name"] = symbol.name();
    if (!detectedFootprint.isEmpty()) out["footprint"] = detectedFootprint;
    printJsonValue(out);
    return true;
}

bool runLibraryIndex(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 2) {
        std::cerr << "Usage: viora library-index <folder>" << std::endl;
        return false;
    }
    const QString root = args.at(1);
    QDir dir(root);
    if (!dir.exists()) {
        std::cerr << "Error: Folder not found: " << root.toStdString() << std::endl;
        return false;
    }

    QJsonObject out;
    out["root"] = root;

    // Symbols (.viosym and .sclib)
    QJsonArray symbols;
    {
        QDirIterator it(root, QStringList() << "*.viosym", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            QFile f(filePath);
            QString name = QFileInfo(filePath).completeBaseName();
            if (f.open(QIODevice::ReadOnly)) {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
                f.close();
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    if (obj.contains("name")) {
                        const QString n = obj.value("name").toString().trimmed();
                        if (!n.isEmpty()) name = n;
                    }
                }
            }
            QJsonObject s;
            s["name"] = name;
            s["path"] = filePath;
            s["type"] = "viosym";
            symbols.append(s);
        }

        QDirIterator libIt(root, QStringList() << "*.sclib", QDir::Files, QDirIterator::Subdirectories);
        while (libIt.hasNext()) {
            QString filePath = libIt.next();
            SymbolLibrary lib;
            if (!lib.load(filePath)) continue;
            for (const QString& symName : lib.symbolNames()) {
                QJsonObject s;
                s["name"] = symName;
                s["path"] = filePath;
                s["type"] = "sclib";
                symbols.append(s);
            }
        }
    }
    out["symbols"] = symbols;

    const bool includeComments = parser.isSet("include-comments");

    // Models (.lib, .sub, .cmp, .cir, .sp, .txt) + parsed names
    QJsonArray models;
    QMap<QString, QSet<QString>> modelIndex;
    QMap<QString, QSet<QString>> subcktIndex;
    {
        QDirIterator it(root, QStringList() << "*.lib" << "*.sub" << "*.cmp" << "*.cir" << "*.sp" << "*.txt",
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            QJsonObject m;
            m["path"] = filePath;
            m["type"] = QFileInfo(filePath).suffix().toLower();

            QSet<QString> subcktSet;
            QSet<QString> modelSet;
            QFile f(filePath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&f);
                QRegularExpression subcktRe("^\\s*\\.subckt\\s+([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                QRegularExpression modelRe("^\\s*\\.model\\s+([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                while (!in.atEnd()) {
                    QString line = in.readLine();
                    QString candidate = line;
                    if (includeComments) {
                        QString trimmed = line.trimmed();
                        if (trimmed.startsWith('*') || trimmed.startsWith(';')) {
                            trimmed.remove(0, 1);
                            candidate = trimmed.trimmed();
                        }
                    } else {
                        QString trimmed = line.trimmed();
                        if (trimmed.startsWith('*') || trimmed.startsWith(';')) continue;
                    }
                    auto sm = subcktRe.match(candidate);
                    if (sm.hasMatch()) {
                        const QString name = sm.captured(1).trimmed();
                        if (!name.isEmpty()) subcktSet.insert(name);
                    }
                    auto mm = modelRe.match(candidate);
                    if (mm.hasMatch()) {
                        const QString name = mm.captured(1).trimmed();
                        if (!name.isEmpty()) modelSet.insert(name);
                    }
                }
                f.close();
            }
            QJsonArray subckts;
            for (const auto& name : subcktSet) subckts.append(name);
            QJsonArray modelNames;
            for (const auto& name : modelSet) modelNames.append(name);
            m["subckts"] = subckts;
            m["models"] = modelNames;
            models.append(m);

            for (const auto& name : subcktSet) subcktIndex[name].insert(filePath);
            for (const auto& name : modelSet) modelIndex[name].insert(filePath);
        }
    }
    out["models"] = models;

    QJsonObject indexObj;
    QJsonObject modelMap;
    for (auto it = modelIndex.begin(); it != modelIndex.end(); ++it) {
        QJsonArray paths;
        for (const auto& p : it.value()) paths.append(p);
        modelMap[it.key()] = paths;
    }
    QJsonObject subcktMap;
    for (auto it = subcktIndex.begin(); it != subcktIndex.end(); ++it) {
        QJsonArray paths;
        for (const auto& p : it.value()) paths.append(p);
        subcktMap[it.key()] = paths;
    }
    indexObj["models"] = modelMap;
    indexObj["subckts"] = subcktMap;
    out["modelIndex"] = indexObj;

    printJsonValue(out);
    return true;
}

bool runSchematicRender(const QString& filePath, const QString& outPath, const QCommandLineParser& parser) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    QRectF rect = scene.itemsBoundingRect();
    if (rect.isEmpty()) rect = QRectF(-50, -50, 100, 100);
    rect.adjust(-10, -10, 10, 10);

    const qreal scale = qMax(0.1, parser.value("scale").toDouble());
    QImage image(rect.size().toSize() * scale, QImage::Format_ARGB32);
    const bool transparent = parser.isSet("transparent");
    image.fill(transparent ? Qt::transparent : QColor(20, 20, 25));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    scene.render(&painter, QRectF(), rect);
    painter.end();

    if (!image.save(outPath)) {
        std::cerr << "Failed to save image to " << outPath.toStdString() << std::endl;
        return false;
    }

    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = filePath;
        out["output"] = outPath;
        out["width"] = image.width();
        out["height"] = image.height();
        out["scale"] = scale;
        out["transparent"] = transparent;
        QJsonObject bounds;
        bounds["x"] = rect.x();
        bounds["y"] = rect.y();
        bounds["w"] = rect.width();
        bounds["h"] = rect.height();
        out["bounds"] = bounds;
        printJsonValue(out);
    } else {
        printInfoStd("Successfully rendered schematic to " + outPath.toStdString());
    }
    return true;
}

bool runGenerateReport(const QString& schematicPath, const QString& outPath, const QCommandLineParser& parser) {
    SimReportGenerator generator;
    SimReportGenerator::ReportOptions opts;
    opts.title = parser.value("report-title");
    opts.author = parser.value("report-author");
    opts.includeSchematic = !parser.isSet("no-schematic");
    opts.includeWaveforms = !parser.isSet("no-waveforms");
    opts.includeMeasurements = !parser.isSet("no-measurements");
    opts.includeNetlist = !parser.isSet("no-netlist");
    opts.maxWaveformPoints = parser.value("max-points").toInt();
    if (opts.maxWaveformPoints == 0) opts.maxWaveformPoints = 1000;
    
    generator.setSchematicPath(schematicPath);
    generator.setOptions(opts);
    
    const QString rawFilePath = parser.value("raw-file");
    if (!rawFilePath.isEmpty()) {
        RawData data;
        QString error;
        if (RawDataParser::loadRawAscii(rawFilePath.toStdString(), &data)) {
            SimResults results = data.toSimResults();
            generator.setSimulationResults(results);
            
            QString netlistPath = rawFilePath;
            netlistPath.replace(".raw", ".cir");
            QFile netlistFile(netlistPath);
            if (netlistFile.exists() && netlistFile.open(QIODevice::ReadOnly)) {
                generator.setNetlist(QString::fromUtf8(netlistFile.readAll()));
                netlistFile.close();
            }
        }
    }
    
    const QString schematicPngPath = parser.value("schematic-png");
    if (!schematicPngPath.isEmpty()) {
        QImage schematicImg(schematicPngPath);
        if (!schematicImg.isNull()) {
            generator.setSchematicImage(schematicImg);
        }
    }
    
    if (!generator.saveToFile(outPath)) {
        std::cerr << "Error: Failed to save report to " << outPath.toStdString() << std::endl;
        return false;
    }
    
    if (parser.isSet("json")) {
        QJsonObject out;
        out["schematic"] = schematicPath;
        out["report"] = outPath;
        out["success"] = true;
        printJsonValue(out);
    } else {
        printInfoStd("Design review report generated: " + outPath.toStdString());
    }
    return true;
}

bool runShareSchematic(const QString& schematicPath, const QCommandLineParser& parser) {
    QString title = parser.value("share-title");
    QString description = parser.value("share-description");
    bool upload = parser.isSet("upload");
    bool copyToClipboard = parser.isSet("copy");
    
    QByteArray data = SchematicUrlEncoder::serializeToCompact(schematicPath);
    if (data.isEmpty()) {
        std::cerr << "Error: Failed to read schematic file: " << schematicPath.toStdString() << std::endl;
        return false;
    }
    
    bool fitsInUrl = SchematicUrlEncoder::fitsInUrl(data);
    
    if (upload || !fitsInUrl) {
        QString serverUrl = parser.value("server");
        if (serverUrl.isEmpty()) {
            serverUrl = "http://localhost:8765";
        }
        
        QNetworkRequest request(serverUrl + "/api/share");
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        
        QJsonObject body;
        body["schematic"] = QString::fromUtf8(data);
        body["title"] = title;
        body["description"] = description;
        
        QNetworkAccessManager* mgr = new QNetworkAccessManager();
        QNetworkReply* reply = mgr->post(request, QJsonDocument(body).toJson());
        
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        QByteArray response = reply->readAll();
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        
        if (statusCode == 201) {
            QJsonObject result = QJsonDocument::fromJson(response).object();
            QString shortUrl = result.value("shortUrl").toString();
            QString fullUrl = result.value("fullUrl").toString();
            
            if (parser.isSet("json")) {
                QJsonObject out;
                out["success"] = true;
                out["url"] = fullUrl;
                out["shortUrl"] = shortUrl;
                out["expiresAt"] = result.value("expiresAt").toString();
                printJsonValue(out);
            } else {
                printInfoStd("Schematic shared: " + fullUrl.toStdString());
                if (copyToClipboard || parser.value("copy").isEmpty()) {
                    printInfoStd("URL copied to clipboard");
                }
            }
        } else {
            std::cerr << "Error: Upload failed - " << response.constData() << std::endl;
            return false;
        }
        
        reply->deleteLater();
    } else {
        QString encoded = SchematicUrlEncoder::encodeForUrl(data);
        QString url = "viospice://open?data=" + encoded;
        
        if (parser.isSet("json")) {
            QJsonObject out;
            out["success"] = true;
            out["url"] = url;
            out["fitsInUrl"] = true;
            out["size"] = data.size();
            printJsonValue(out);
        } else {
            printInfoStd("Schematic URL (fits in clipboard): " + url.toStdString());
            if (copyToClipboard || parser.value("copy").isEmpty()) {
                printInfoStd("URL copied to clipboard");
            }
        }
    }
    
    return true;
}

bool runSymbolQuery(const QStringList& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: viora symbol-query <file.viosym>" << std::endl;
        return false;
    }
    const QString filePath = args.at(1);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read symbol file: " << filePath.toStdString() << std::endl;
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Error: Invalid symbol JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }
    QJsonObject obj = doc.object();
    if (obj.contains("library")) {
        std::cerr << "Error: This looks like a library file (.sclib), not a .viosym." << std::endl;
        return false;
    }

    SymbolDefinition symbol = SymbolDefinition::fromJson(obj);
    if (symbol.name().trimmed().isEmpty()) {
        symbol.setName(QFileInfo(filePath).completeBaseName());
    }

    QJsonObject out;
    out["file"] = filePath;
    out["name"] = symbol.name();
    out["description"] = symbol.description();
    out["category"] = symbol.category();
    out["referencePrefix"] = symbol.referencePrefix();
    out["defaultValue"] = symbol.defaultValue();
    out["modelSource"] = symbol.modelSource();
    out["modelPath"] = symbol.modelPath();
    out["modelName"] = symbol.modelName();
    int pinCount = 0;
    for (const auto& prim : symbol.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) pinCount++;
    }
    out["pinCount"] = pinCount;

    QJsonArray pins;
    for (const auto& prim : symbol.primitives()) {
        if (prim.type != SymbolPrimitive::Pin) continue;
        QJsonObject p;
        p["number"] = prim.data.value("number").toInt();
        p["name"] = prim.data.value("name").toString();
        p["x"] = prim.data.value("x").toDouble();
        p["y"] = prim.data.value("y").toDouble();
        p["orientation"] = prim.data.value("orientation").toString();
        p["length"] = prim.data.value("length").toDouble(15.0);
        p["visible"] = prim.data.value("visible").toBool(true);
        pins.append(p);
    }
    out["pins"] = pins;

    QJsonObject bounds;
    const QRectF rect = symbol.boundingRect();
    bounds["x"] = rect.x();
    bounds["y"] = rect.y();
    bounds["w"] = rect.width();
    bounds["h"] = rect.height();
    out["boundingRect"] = bounds;

    printJsonValue(out);
    return true;
}

bool runSymbolValidate(const QStringList& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: viora symbol-validate <file.viosym>" << std::endl;
        return false;
    }
    const QString filePath = args.at(1);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read symbol file: " << filePath.toStdString() << std::endl;
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Error: Invalid symbol JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }
    QJsonObject obj = doc.object();
    if (obj.contains("library")) {
        std::cerr << "Error: This looks like a library file (.sclib), not a .viosym." << std::endl;
        return false;
    }

    SymbolDefinition symbol = SymbolDefinition::fromJson(obj);
    if (symbol.name().trimmed().isEmpty()) {
        symbol.setName(QFileInfo(filePath).completeBaseName());
    }

    QJsonArray issues;
    auto addIssue = [&](const QString& severity, const QString& message) {
        QJsonObject i;
        i["severity"] = severity;
        i["message"] = message;
        issues.append(i);
    };

    if (symbol.name().trimmed().isEmpty()) {
        addIssue("Error", "Symbol name is empty.");
    }
    if (symbol.referencePrefix().trimmed().isEmpty()) {
        addIssue("Warning", "Reference prefix is empty.");
    }

    int pinCount = 0;
    for (const auto& prim : symbol.primitives()) {
        if (prim.type != SymbolPrimitive::Pin) continue;
        pinCount++;
        const QString pinName = prim.data.value("name").toString().trimmed();
        const int number = prim.data.value("number").toInt();
        const QString orient = prim.data.value("orientation").toString("Right");
        const qreal len = prim.data.value("length").toDouble(15.0);
        if (number <= 0) {
            addIssue("Warning", QString("Pin has invalid number (%1).").arg(number));
        }
        if (pinName.isEmpty()) {
            addIssue("Warning", QString("Pin %1 has empty name.").arg(number > 0 ? QString::number(number) : "?"));
        }
        if (orient != "Left" && orient != "Right" && orient != "Up" && orient != "Down") {
            addIssue("Warning", QString("Pin %1 has invalid orientation '%2'.").arg(number > 0 ? QString::number(number) : "?", orient));
        }
        if (len <= 0.0) {
            addIssue("Warning", QString("Pin %1 has non-positive length.").arg(number > 0 ? QString::number(number) : "?"));
        }
    }
    if (pinCount == 0) {
        addIssue("Error", "Symbol has no pins.");
    }

    const QString modelName = symbol.modelName().trimmed();
    const QString modelPath = symbol.modelPath().trimmed();
    const QString modelSource = symbol.modelSource().trimmed().toLower();
    const QSet<QString> validSources = {"", "none", "library", "project", "absolute"};
    if (!modelSource.isEmpty() && !validSources.contains(modelSource)) {
        addIssue("Warning", QString("Model source '%1' is not recognized.").arg(symbol.modelSource()));
    }
    if (!modelName.isEmpty() && modelPath.isEmpty()) {
        addIssue("Warning", "Model name is set but model file path is empty.");
    }
    if (modelName.isEmpty() && !modelPath.isEmpty()) {
        addIssue("Warning", "Model file path is set but model name is empty.");
    }

    QJsonObject out;
    out["file"] = filePath;
    out["name"] = symbol.name();
    out["pinCount"] = pinCount;
    out["issues"] = issues;
    QJsonObject summary;
    summary["count"] = issues.size();
    out["summary"] = summary;

    printJsonValue(out);
    return true;
}

double parseNumericValue(const QString& val) {
    if (val.isEmpty()) return 0.0;
    QString s = val.trimmed().toLower();
    double multiplier = 1.0;
    if (s.endsWith("k")) { multiplier = 1e3; s.chop(1); }
    else if (s.endsWith("m") && !s.endsWith("meg")) { multiplier = 1e-3; s.chop(1); }
    else if (s.endsWith("u")) { multiplier = 1e-6; s.chop(1); }
    else if (s.endsWith("n")) { multiplier = 1e-9; s.chop(1); }
    else if (s.endsWith("p")) { multiplier = 1e-12; s.chop(1); }
    else if (s.endsWith("meg")) { multiplier = 1e6; s.chop(3); }
    else if (s.endsWith("g")) { multiplier = 1e9; s.chop(1); }
    bool ok;
    double d = s.toDouble(&ok);
    return ok ? d * multiplier : 0.0;
}

bool runSchematicQuery(const QString& filePath) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    QJsonObject out;
    out["file"] = filePath;
    out["pageSize"] = pageSize;

    ECOPackage pkg = NetlistGenerator::generateECOPackage(&scene, QFileInfo(filePath).absolutePath(), nullptr);
    QList<NetlistNet> nets = NetlistGenerator::buildConnectivity(&scene, QFileInfo(filePath).absolutePath(), nullptr);

    QJsonArray comps;
    for (const auto& comp : pkg.components) {
        QJsonObject c;
        c["reference"] = comp.reference;
        c["typeName"] = comp.typeName;
        c["type"] = comp.type;
        c["value"] = comp.value;
        c["spiceModel"] = comp.spiceModel;
        c["footprint"] = comp.footprint;
        c["symbolPinCount"] = comp.symbolPinCount;
        c["excludeFromSim"] = comp.excludeFromSim;
        c["excludeFromPcb"] = comp.excludeFromPcb;
        comps.append(c);
    }
    out["components"] = comps;

    QJsonArray netsArr;
    for (const auto& net : nets) {
        QJsonObject n;
        n["name"] = net.name;
        QJsonArray pins;
        for (const auto& pin : net.pins) {
            QJsonObject p;
            p["ref"] = pin.componentRef;
            p["pin"] = pin.pinName;
            pins.append(p);
        }
        n["pins"] = pins;
        netsArr.append(n);
    }
    out["nets"] = netsArr;

    printJsonValue(out);
    return true;
}

bool runSchematicNetlist(const QString& filePath, const QCommandLineParser& parser) {
    const QString format = parser.value("format").trimmed().toLower();
    const QString outPath = parser.value("out").trimmed();
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    if (format == "json") {
        const QString net = NetlistGenerator::generate(&scene, QFileInfo(filePath).absolutePath(), NetlistGenerator::FluxJSON, nullptr);
        if (!outPath.isEmpty()) {
            QFile outFile(outPath);
            if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                std::cerr << "Error: Unable to write netlist to " << outPath.toStdString() << std::endl;
                return false;
            }
            outFile.write(net.toUtf8());
            outFile.close();
        } else {
            std::cout << net.toStdString() << std::endl;
        }
        return true;
    }

    SpiceNetlistGenerator::SimulationParams params;
    QString analysisType = parser.value("analysis").toLower();
    if (analysisType == "tran") {
        params.type = SpiceNetlistGenerator::Transient;
        params.step = parser.value("step").isEmpty() ? "1e-6" : parser.value("step");
        params.stop = parser.value("stop").isEmpty() ? "1e-2" : parser.value("stop");
    } else if (analysisType == "ac") {
        params.type = SpiceNetlistGenerator::AC;
        params.start = "10";
        params.stop = "1e6";
    } else {
        params.type = SpiceNetlistGenerator::OP;
    }

    auto result = SpiceNetlistGenerator::generate(&scene, QFileInfo(filePath).absolutePath(), nullptr, params);
    if (!outPath.isEmpty()) {
        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::cerr << "Error: Unable to write netlist to " << outPath.toStdString() << std::endl;
            return false;
        }
        outFile.write(result.netlist.toUtf8());
        outFile.close();
    } else {
        std::cout << result.netlist.toStdString() << std::endl;
    }
    return true;
}

QJsonObject componentToJson(const ECOComponent& comp) {
    QJsonObject c;
    c["reference"] = comp.reference;
    c["typeName"] = comp.typeName;
    c["type"] = comp.type;
    c["value"] = comp.value;
    c["spiceModel"] = comp.spiceModel;
    c["footprint"] = comp.footprint;
    c["symbolPinCount"] = comp.symbolPinCount;
    c["excludeFromSim"] = comp.excludeFromSim;
    c["excludeFromPcb"] = comp.excludeFromPcb;
    return c;
}

QMap<QString, QPointF> collectComponentPositions(QGraphicsScene* scene) {
    QMap<QString, QPointF> out;
    if (!scene) return out;
    for (auto* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            const QString ref = si->reference();
            if (!ref.trimmed().isEmpty() && !out.contains(ref)) {
                out[ref] = si->pos();
            }
        }
    }
    return out;
}

bool runSchematicValidate(const QString& filePath) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    QJsonObject out;
    out["file"] = filePath;
    out["pageSize"] = pageSize;

    // ERC
    QJsonArray ercIssues;
    auto violations = SchematicERC::run(&scene, QFileInfo(filePath).absolutePath());
    for (const auto& v : violations) {
        QJsonObject issue;
        issue["severity"] = (v.severity == ERCViolation::Error) ? "Error" :
                            (v.severity == ERCViolation::Critical) ? "Critical" : "Warning";
        issue["message"] = v.message;
        issue["x"] = v.position.x();
        issue["y"] = v.position.y();
        ercIssues.append(issue);
    }
    out["erc"] = ercIssues;

    // Simulator preflight (ground, model resolution, pin mismatch)
    SimNetlist preflightNetlist;
    QStringList preflight = SimManager::instance().preflightCheck(&scene, nullptr, preflightNetlist);
    QJsonArray preflightArr;
    for (const QString& msg : preflight) preflightArr.append(msg);
    out["preflight"] = preflightArr;

    // Summary
    QJsonObject summary;
    summary["ercCount"] = ercIssues.size();
    summary["preflightCount"] = preflightArr.size();
    out["summary"] = summary;

    printJsonValue(out);
    return true;
}

bool runSchematicBom(const QString& filePath) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    ECOPackage pkg = NetlistGenerator::generateECOPackage(&scene, QFileInfo(filePath).absolutePath(), nullptr);
    QJsonObject out;
    out["file"] = filePath;

    // Flat component list (sorted by reference)
    std::sort(pkg.components.begin(), pkg.components.end(), [](const ECOComponent& a, const ECOComponent& b) {
        return a.reference.toLower() < b.reference.toLower();
    });
    QJsonArray comps;
    for (const auto& comp : pkg.components) {
        comps.append(componentToJson(comp));
    }
    out["components"] = comps;

    // Grouped BOM
    struct BomKey {
        QString typeName;
        QString value;
        QString footprint;
        bool operator<(const BomKey& other) const {
            if (typeName != other.typeName) return typeName < other.typeName;
            if (value != other.value) return value < other.value;
            return footprint < other.footprint;
        }
    };
    QMap<BomKey, QStringList> groups;
    for (const auto& comp : pkg.components) {
        BomKey key{comp.typeName, comp.value, comp.footprint};
        groups[key].append(comp.reference);
    }
    QJsonArray grouped;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QJsonObject g;
        g["typeName"] = it.key().typeName;
        g["value"] = it.key().value;
        g["footprint"] = it.key().footprint;
        g["qty"] = it.value().size();
        QStringList refs = it.value();
        std::sort(refs.begin(), refs.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
        QJsonArray refArr;
        for (const QString& r : refs) refArr.append(r);
        g["references"] = refArr;
        grouped.append(g);
    }
    out["groups"] = grouped;

    printJsonValue(out);
    return true;
}

bool runSchematicDiff(const QStringList& args) {
    if (args.size() < 3) {
        std::cerr << "Usage: viora schematic-diff <a.flxsch> <b.flxsch>" << std::endl;
        return false;
    }
    const QString aPath = args.at(1);
    const QString bPath = args.at(2);

    QGraphicsScene sceneA;
    QString pageA;
    TitleBlockData tbA;
    if (!SchematicFileIO::loadSchematic(&sceneA, aPath, pageA, tbA)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }
    QGraphicsScene sceneB;
    QString pageB;
    TitleBlockData tbB;
    if (!SchematicFileIO::loadSchematic(&sceneB, bPath, pageB, tbB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    ECOPackage pkgA = NetlistGenerator::generateECOPackage(&sceneA, QFileInfo(aPath).absolutePath(), nullptr);
    ECOPackage pkgB = NetlistGenerator::generateECOPackage(&sceneB, QFileInfo(bPath).absolutePath(), nullptr);

    QMap<QString, ECOComponent> mapA;
    QMap<QString, ECOComponent> mapB;
    for (const auto& c : pkgA.components) mapA[c.reference] = c;
    for (const auto& c : pkgB.components) mapB[c.reference] = c;

    QMap<QString, QPointF> posA = collectComponentPositions(&sceneA);
    QMap<QString, QPointF> posB = collectComponentPositions(&sceneB);

    QJsonObject out;
    out["a"] = aPath;
    out["b"] = bPath;

    QJsonArray added;
    QJsonArray removed;
    QJsonArray changed;

    for (auto it = mapA.begin(); it != mapA.end(); ++it) {
        const QString ref = it.key();
        if (!mapB.contains(ref)) {
            removed.append(ref);
            continue;
        }
        const ECOComponent& ca = it.value();
        const ECOComponent& cb = mapB[ref];
        QJsonObject diff;
        bool hasDiff = false;
        if (ca.typeName != cb.typeName) { diff["typeName"] = QJsonArray{ca.typeName, cb.typeName}; hasDiff = true; }
        if (ca.value != cb.value) { diff["value"] = QJsonArray{ca.value, cb.value}; hasDiff = true; }
        if (ca.footprint != cb.footprint) { diff["footprint"] = QJsonArray{ca.footprint, cb.footprint}; hasDiff = true; }
        if (ca.spiceModel != cb.spiceModel) { diff["spiceModel"] = QJsonArray{ca.spiceModel, cb.spiceModel}; hasDiff = true; }
        if (posA.contains(ref) && posB.contains(ref)) {
            QPointF pa = posA[ref];
            QPointF pb = posB[ref];
            if (pa != pb) {
                QJsonObject pos;
                pos["from"] = QJsonArray{pa.x(), pa.y()};
                pos["to"] = QJsonArray{pb.x(), pb.y()};
                diff["position"] = pos;
                hasDiff = true;
            }
        }
        if (hasDiff) {
            QJsonObject entry;
            entry["reference"] = ref;
            entry["changes"] = diff;
            changed.append(entry);
        }
    }
    for (auto it = mapB.begin(); it != mapB.end(); ++it) {
        const QString ref = it.key();
        if (!mapA.contains(ref)) added.append(ref);
    }

    out["components"] = QJsonObject{
        {"added", added},
        {"removed", removed},
        {"changed", changed}
    };

    printJsonValue(out);
    return true;
}

bool runSchematicTransform(const QString& filePath, const QCommandLineParser& parser) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    const QStringList renames = parser.values("rename-net");
    const QStringList valueRules = parser.values("normalize-value");
    const QString prefixRule = parser.value("prefix-ref");

    QMap<QString, QString> netRename;
    for (const QString& rule : renames) {
        const int eq = rule.indexOf('=');
        if (eq > 0) {
            const QString from = rule.left(eq).trimmed();
            const QString to = rule.mid(eq + 1).trimmed();
            if (!from.isEmpty() && !to.isEmpty()) netRename[from] = to;
        }
    }

    QMap<QString, QString> valueMap;
    for (const QString& rule : valueRules) {
        const int eq = rule.indexOf('=');
        if (eq > 0) {
            const QString from = rule.left(eq).trimmed();
            const QString to = rule.mid(eq + 1).trimmed();
            if (!from.isEmpty() && !to.isEmpty()) valueMap[from] = to;
        }
    }

    QString prefixFrom;
    QString prefixTo;
    if (!prefixRule.isEmpty()) {
        const int eq = prefixRule.indexOf('=');
        if (eq > 0) {
            prefixFrom = prefixRule.left(eq).trimmed();
            prefixTo = prefixRule.mid(eq + 1).trimmed();
        }
    }

    int renamedNets = 0;
    int normalizedValues = 0;
    int updatedRefs = 0;

    for (auto* item : scene.items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            // Net label rename
            if (si->itemType() == SchematicItem::NetLabelType) {
                const QString cur = si->value();
                if (netRename.contains(cur)) {
                    si->setValue(netRename[cur]);
                    renamedNets++;
                }
            }

            // Normalize values
            const QString val = si->value();
            if (valueMap.contains(val)) {
                si->setValue(valueMap[val]);
                normalizedValues++;
            }

            // Prefix rename
            const QString ref = si->reference();
            if (!prefixFrom.isEmpty() && ref.startsWith(prefixFrom)) {
                si->setReference(prefixTo + ref.mid(prefixFrom.size()));
                updatedRefs++;
            }
        }
    }

    if (!SchematicFileIO::saveSchematic(&scene, filePath, pageSize)) {
        std::cerr << "Error saving schematic." << std::endl;
        return false;
    }

    QJsonObject out;
    out["file"] = filePath;
    out["renamedNets"] = renamedNets;
    out["normalizedValues"] = normalizedValues;
    out["updatedRefs"] = updatedRefs;
    printJsonValue(out);
    return true;
}

bool runSchematicProbe(const QString& filePath, const QCommandLineParser& parser) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    SimNetlist netlist = SimSchematicBridge::buildNetlist(&scene, nullptr);

    const bool listSignals = parser.isSet("list");
    const QStringList addSignals = parser.values("add");
    const bool autoProbe = parser.isSet("auto");
    const bool outputJson = parser.isSet("json");

    if (listSignals || (addSignals.isEmpty() && !autoProbe)) {
        QStringList voltageSignals;
        QStringList currentSignals;

        for (int i = 0; i < netlist.nodeCount(); ++i) {
            const QString name = QString::fromStdString(netlist.nodeName(i)).trimmed();
            if (name.isEmpty()) continue;
            voltageSignals.append("V(" + name + ")");
        }

        for (const auto& comp : netlist.components()) {
            const QString name = QString::fromStdString(comp.name).trimmed();
            if (name.isEmpty()) continue;
            
            // Simplified branch current detection for CLI signal listing
            bool hasBranch = (comp.type == SimComponentType::VoltageSource || 
                              comp.type == SimComponentType::Inductor ||
                              comp.type == SimComponentType::B_VoltageSource ||
                              comp.type == SimComponentType::OpAmpMacro);
            
            if (!hasBranch) continue;
            currentSignals.append("I(" + name + ")");
        }

        voltageSignals.sort(Qt::CaseInsensitive);
        currentSignals.sort(Qt::CaseInsensitive);

        QJsonArray voltageJson;
        for (const QString& v : voltageSignals) voltageJson.append(v);

        QJsonArray currentJson;
        for (const QString& c : currentSignals) currentJson.append(c);

        QJsonArray allSignals;
        for (const QString& v : voltageSignals) allSignals.append(v);
        for (const QString& c : currentSignals) allSignals.append(c);

        QJsonObject out;
        out["file"] = filePath;
        out["signals"] = allSignals;
        out["voltages"] = voltageJson;
        out["currents"] = currentJson;
        printJsonValue(out);
        return true;
    }

    // Append probes into netlist (for future simulation selection)
    if (autoProbe) {
        for (int i = 0; i < netlist.nodeCount(); ++i) {
            const QString name = QString::fromStdString(netlist.nodeName(i)).trimmed();
            if (name.isEmpty()) continue;
            netlist.addAutoProbe(("V(" + name + ")").toStdString());
        }
    } else {
        for (const QString& sig : addSignals) {
            netlist.addAutoProbe(sig.toStdString());
        }
    }

    QJsonObject out;
    out["file"] = filePath;
    QJsonArray probes;
    for (const auto& p : netlist.autoProbes()) {
        probes.append(QString::fromStdString(p));
    }
    out["probes"] = probes;
    printJsonValue(out);
    Q_UNUSED(outputJson);
    return true;
}

bool runNetlistCompare(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 3) {
        std::cerr << "Usage: viora netlist-compare <file.flxsch> <external.net>" << std::endl;
        return false;
    }
    const QString schematicPath = args.at(1);
    const QString externalPath = args.at(2);

    if (!QFileInfo::exists(schematicPath)) {
        std::cerr << "Error: Schematic not found: " << schematicPath.toStdString() << std::endl;
        return false;
    }
    if (!QFileInfo::exists(externalPath)) {
        std::cerr << "Error: Netlist not found: " << externalPath.toStdString() << std::endl;
        return false;
    }

    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, schematicPath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    SpiceNetlistGenerator::SimulationParams params;
    QString analysisType = parser.value("analysis").toLower();
    if (analysisType == "tran") {
        params.type = SpiceNetlistGenerator::Transient;
        params.step = parser.value("step").isEmpty() ? "1e-6" : parser.value("step");
        params.stop = parser.value("stop").isEmpty() ? "1e-2" : parser.value("stop");
    } else if (analysisType == "ac") {
        params.type = SpiceNetlistGenerator::AC;
        params.start = "10";
        params.stop = "1e6";
    } else {
        params.type = SpiceNetlistGenerator::OP;
    }

    auto result = SpiceNetlistGenerator::generate(&scene, QFileInfo(schematicPath).absolutePath(), nullptr, params);

    QFile extFile(externalPath);
    if (!extFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "Error: Cannot read netlist file: " << externalPath.toStdString() << std::endl;
        return false;
    }
    const QString externalNetlist = QString::fromUtf8(extFile.readAll());
    extFile.close();

    const QStringList schLines = normalizeNetlistText(result.netlist);
    const QStringList extLines = normalizeNetlistText(externalNetlist);

    QMap<QString, int> schCounts;
    for (const QString& l : schLines) schCounts[l]++;
    QMap<QString, int> extCounts;
    for (const QString& l : extLines) extCounts[l]++;

    QSet<QString> allKeys = QSet<QString>(schCounts.keyBegin(), schCounts.keyEnd());
    allKeys.unite(QSet<QString>(extCounts.keyBegin(), extCounts.keyEnd()));

    QJsonArray diffs;
    int onlySch = 0;
    int onlyExt = 0;
    int common = 0;

    for (const QString& key : allKeys) {
        const int a = schCounts.value(key);
        const int b = extCounts.value(key);
        if (a == b && a > 0) {
            common += a;
            continue;
        }
        if (a > b) onlySch += (a - b);
        if (b > a) onlyExt += (b - a);
        QJsonObject d;
        d["line"] = key;
        d["schematicCount"] = a;
        d["externalCount"] = b;
        diffs.append(d);
    }

    QJsonObject out;
    out["schematic"] = schematicPath;
    out["external"] = externalPath;
    out["schematicLineCount"] = schLines.size();
    out["externalLineCount"] = extLines.size();
    out["differences"] = diffs;
    QJsonObject summary;
    summary["common"] = common;
    summary["onlyInSchematic"] = onlySch;
    summary["onlyInExternal"] = onlyExt;
    summary["differentLineCount"] = diffs.size();
    out["summary"] = summary;
    printJsonValue(out);
    return true;
}

bool runNetlistRun(const QString& filePath, const QCommandLineParser& parser) {
    if (!QFileInfo::exists(filePath)) {
        std::cerr << "Error: Netlist not found: " << filePath.toStdString() << std::endl;
        return false;
    }

    auto& sim = SimulationManager::instance();
    if (!sim.isAvailable()) {
        std::cerr << "Error: Ngspice not available in this build." << std::endl;
        return false;
    }

    QString timeoutError;
    const auto timeoutMsOpt = parseTimeoutMs(parser.value("timeout"), &timeoutError);
    if (!timeoutMsOpt.has_value()) {
        std::cerr << "Error: " << timeoutError.toStdString() << std::endl;
        return false;
    }
    const int timeoutMs = timeoutMsOpt.value();

    QString runPath = filePath;
    std::unique_ptr<QTemporaryFile> tempNetlist;
    QGraphicsScene scene; // Keep scene alive for JIT blocks during simulation

    const QString suffix = QFileInfo(filePath).suffix().toLower();
    const bool applyCompat = parser.isSet("compat");
    
    // --- Initialize Engine First ---
    sim.initialize();
    
    QObject::connect(&SimManager::instance(), &SimManager::logMessage, &sim, [&](const QString& msg) {
        if (!g_quiet) std::cout << "[Simulator] " << msg.toStdString() << std::endl;
    });
    QObject::connect(&SimManager::instance(), &SimManager::errorOccurred, &sim, [&](const QString& msg) {
        std::cerr << "[Simulator Error] " << msg.toStdString() << std::endl;
    });
    
    if (suffix == "flxsch") {
        QString pageSize;
        TitleBlockData dummyTB;
        if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
            std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
            return false;
        }

        SpiceNetlistGenerator::SimulationParams params;
        QString analysisType = parser.value("analysis").toLower();
        if (analysisType == "tran") {
            params.type = SpiceNetlistGenerator::Transient;
            params.step = parser.value("step").isEmpty() ? "1e-6" : parser.value("step");
            params.stop = parser.value("stop").isEmpty() ? "1e-2" : parser.value("stop");
        } else if (analysisType == "ac") {
            params.type = SpiceNetlistGenerator::AC;
            params.start = "10";
            params.stop = "1e6";
        } else {
            params.type = SpiceNetlistGenerator::OP;
        }

        const auto result = SpiceNetlistGenerator::generate(&scene, QFileInfo(filePath).absolutePath(), nullptr, params);

        // Ensure ngspice sets up its heap and signal handlers BEFORE LLVM JIT is created
        // to prevent malloc_consolidate crashes during simulation stream.

        // --- FluxScript Integration ---
        if (!g_quiet) std::cout << "FluxScript: Found " << result.componentPins.size() << " component pin mappings." << std::endl;
        for (auto it = result.componentPins.begin(); it != result.componentPins.end(); ++it) {
            if (!g_quiet) std::cout << "  " << it.key().toStdString() << ": " << it.value().size() << " pins." << std::endl;
        }

        SimManager::instance().m_pinToNetMap = result.componentPins;
        SimManager::instance().compileFluxScripts(&scene);
        // ------------------------------

        QString netlistText = result.netlist;
        if (parser.isSet("robust")) {
            netlistText += "\n.options gmin=1e-9 abstol=1e-10 reltol=1e-3 chgtol=1e-13 method=gear\n";
            netlistText += ".options rshunt=10Meg\n";
        }

        const QString baseName = QFileInfo(filePath).completeBaseName();
        const QString tempPattern = QDir::tempPath() + "/viospice_netlist_" + baseName + "_XXXXXX.cir";
        tempNetlist = std::make_unique<QTemporaryFile>(tempPattern);
        if (!tempNetlist->open()) {
            std::cerr << "Error: Failed to create temporary netlist." << std::endl;
            return false;
        }
        tempNetlist->write(netlistText.toUtf8());
        tempNetlist->flush();
        runPath = tempNetlist->fileName();
    } else if ((applyCompat || parser.isSet("robust")) && suffix == "cir") {
        QFile inFile(filePath);
        if (!inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            std::cerr << "Error: Cannot read netlist file: " << filePath.toStdString() << std::endl;
            return false;
        }
        QString rawNetlist = QString::fromUtf8(inFile.readAll());
        inFile.close();

        QString finalNetlist = applyCompat ? SpiceNetlistGenerator::generateCompatibilityLayer(rawNetlist) : rawNetlist;
        if (parser.isSet("robust")) {
            finalNetlist += "\n.options gmin=1e-9 abstol=1e-10 reltol=1e-3 chgtol=1e-13 method=gear\n";
            finalNetlist += ".options rshunt=10Meg\n";
        }

        const QString baseName = QFileInfo(filePath).completeBaseName();
        const QString tempPattern = QDir::tempPath() + "/viospice_run_" + baseName + "_XXXXXX.cir";
        tempNetlist = std::make_unique<QTemporaryFile>(tempPattern);
        if (!tempNetlist->open()) {
            std::cerr << "Error: Failed to create temporary netlist." << std::endl;
            return false;
        }
        tempNetlist->write(finalNetlist.toUtf8());
        tempNetlist->flush();
        runPath = tempNetlist->fileName();
    }

    const bool exportRaw = parser.isSet("export-raw");
    const bool exportStats = parser.isSet("stats");
    const QStringList measureExprs = parser.values("measure");
    const QStringList assertExprs = parser.values("assert");
    const bool exportMeasures = !measureExprs.isEmpty();
    const bool runAssertions = !assertExprs.isEmpty();
    const bool exportRequested = exportRaw || exportStats || exportMeasures || runAssertions;
    const QString measureFormat = parser.value("measure-format").trimmed().toLower();
    const bool measureFormatJson = (measureFormat == "json");
    if (!measureFormat.isEmpty() && measureFormat != "text" && measureFormat != "json") {
        std::cerr << "Error: Invalid --measure-format. Use text or json." << std::endl;
        return false;
    }

    QStringList outputs;
    QStringList warnings;
    QString errorMsg;
    bool finished = false;

    QObject::connect(&sim, &SimulationManager::outputReceived, &sim, [&](const QString& msg) {
        const QString trimmed = msg.trimmed();
        outputs << trimmed;
        if (isWarningLine(trimmed)) warnings << trimmed;
    });
    QObject::connect(&sim, &SimulationManager::errorOccurred, &sim, [&](const QString& msg) {
        errorMsg = msg;
    });
    QObject::connect(&sim, qOverload<>(&SimulationManager::simulationFinished), &sim, [&]() {
        finished = true;
    });

    QObject::connect(&SimManager::instance(), &SimManager::logMessage, &sim, [&](const QString& msg) {
        if (!g_quiet) std::cout << "[Simulator] " << msg.toStdString() << std::endl;
    });
    QObject::connect(&SimManager::instance(), &SimManager::errorOccurred, &sim, [&](const QString& msg) {
        std::cerr << "[Simulator Error] " << msg.toStdString() << std::endl;
    });

    {
        const bool jsonOut = parser.isSet("json");
        const bool restoreFd = jsonOut || exportRequested;
        const bool silenceFd = g_quiet || jsonOut;
        ScopedFdSilence silence(silenceFd, restoreFd);
        
        SimControl control;
        sim.runSimulation(runPath, &control);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&sim, qOverload<>(&SimulationManager::simulationFinished), &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        if (timeoutMs > 0) {
            timer.start(timeoutMs);
        }
        loop.exec();
    }

    const bool timedOut = !finished;
    QThread::msleep(100); // Cooldown to ensure background thread processing of results finishes
    
    const QFileInfo info(runPath);
    const QString rawPath = info.absolutePath() + "/" + info.completeBaseName() + ".raw";
    QString exportRawFormat = parser.value("export-raw").trimmed().toLower();
    if (exportRaw && exportRawFormat.isEmpty()) exportRawFormat = "json";
    bool maxOk = false;
    const int maxPoints = parser.value("max-points").toInt(&maxOk);
    const int maxPointsValue = maxOk ? maxPoints : 0;
    double tStart = std::numeric_limits<double>::quiet_NaN();
    double tEnd = std::numeric_limits<double>::quiet_NaN();
    QString rangeError;
    if (!parseRangeOption(parser.value("range"), &tStart, &tEnd, &rangeError)) {
        std::cerr << "Error: " << rangeError.toStdString() << std::endl;
        return false;
    }

    const bool hasWarnings = !warnings.isEmpty();
    const bool okResult = finished && errorMsg.isEmpty() && !timedOut;
    const bool okForExit = okResult && !(g_exitOnWarning && hasWarnings);

    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = runPath;
        out["ok"] = okForExit;
        out["timeout"] = timedOut;
        if (!errorMsg.isEmpty()) out["error"] = errorMsg;
        QJsonArray log;
        for (const QString& line : outputs) log.append(line);
        out["log"] = log;
        out["rawPath"] = rawPath;
        if (hasWarnings) {
            QJsonArray warnArr;
            for (const QString& w : warnings) warnArr.append(w);
            out["warnings"] = warnArr;
        }
        out["hasWarnings"] = hasWarnings;

        if (exportRequested && okResult) {
            RawData data;
            QString err;
            if (RawDataParser::loadRawAscii(rawPath.toStdString(), &data)) {
                QStringList signalNames = parser.values("signal");
                if (signalNames.isEmpty()) {
                    for (int i = 1; i < (int)data.varNames.size(); ++i) {
                        signalNames << QString::fromStdString(data.varNames[i]);
                    }
                }
                QVector<int> indices;
                bool ok = true;
                for (const auto& sig : signalNames) {
                    int idx = -1;
                    for (int j = 0; j < (int)data.varNames.size(); ++j) {
                        if (QString::fromStdString(data.varNames[j]) == sig) {
                            idx = j;
                            break;
                        }
                    }
                    if (idx < 1) { ok = false; break; }
                    indices << (idx - 1);
                }
                const QVector<int> rangeIndices = filteredIndices(data, tStart, tEnd);
                int baseSignalIndex = -1;
                QString baseError;
                if (!resolveBaseSignalIndex(data, parser.value("base-signal"), &baseSignalIndex, &baseError)) {
                    out["error"] = baseError;
                    printJsonValue(out);
                    _Exit(1);
                }
                if (ok && exportRaw) out["raw"] = rawToJson(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex);
                if (ok && exportStats) {
                    const auto stats = computeSignalStats(data, signalNames, indices, rangeIndices);
                    QJsonArray statArr;
                    for (const auto& s : stats) {
                        QJsonObject st;
                        st["name"] = s.name;
                        st["min"] = s.min;
                        st["max"] = s.max;
                        st["avg"] = s.avg;
                        st["rms"] = s.rms;
                        statArr.append(st);
                    }
                    out["stats"] = statArr;
                }
                if (exportMeasures) {
                    QJsonArray measArr;
                    for (const auto& expr : measureExprs) {
                        MeasureRequest req;
                        QString perr;
                        QJsonObject m;
                        m["expr"] = expr;
                        if (!parseMeasure(expr, &req, &perr)) {
                            m["error"] = perr;
                            measArr.append(m);
                            continue;
                        }
                        QStringList varNames;
                        for (const auto& vn : data.varNames) varNames << QString::fromStdString(vn);
                        const int varIndex = findVarIndex(varNames, req.signalName);
                        if (varIndex < 0) {
                            m["error"] = "Signal not found";
                            measArr.append(m);
                            continue;
                        }
                        if (req.type == MeasureType::At) {
                            QVector<double> xData = QVector<double>(data.x.begin(), data.x.end());
                            const int idx = nearestIndex(xData, req.atTime);
                            if (idx < 0) {
                                m["error"] = "No samples";
                            } else if (varIndex == 0) {
                                m["value"] = data.x[idx];
                            } else {
                                m["value"] = data.y[varIndex - 1][idx];
                            }
                        } else {
                            const QVector<int>& samples = rangeIndices;
                            if (samples.isEmpty()) {
                                m["error"] = "No samples in range";
                            } else {
                                const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                                double minVal = vec[samples[0]];
                                double maxVal = vec[samples[0]];
                                double sum = 0.0;
                                double sumSq = 0.0;
                                for (int idx : samples) {
                                    const double v = vec[idx];
                                    if (v < minVal) minVal = v;
                                    if (v > maxVal) maxVal = v;
                                    sum += v;
                                    sumSq += v * v;
                                }
                                if (req.type == MeasureType::Min) m["value"] = minVal;
                                else if (req.type == MeasureType::Max) m["value"] = maxVal;
                                else if (req.type == MeasureType::Pp) m["value"] = (maxVal - minVal);
                                else if (req.type == MeasureType::Rms) m["value"] = std::sqrt(sumSq / samples.size());
                                else m["value"] = (sum / samples.size());
                            }
                        }
                        measArr.append(m);
                    }
                    out["measures"] = measArr;
                }
                
                bool allAssertsPassed = true;
                if (runAssertions) {
                    QJsonArray assertArr;
                    for (const auto& expr : assertExprs) {
                        QJsonObject a;
                        a["expr"] = expr;
                        
                        QRegularExpression re(R"(^(.+?)\s*(==|!=|>=|<=|>|<)\s*(.+)$)");
                        auto match = re.match(expr);
                        if (!match.hasMatch()) {
                            a["error"] = "Invalid assertion format. Use 'signal op value'";
                            a["ok"] = false;
                            allAssertsPassed = false;
                            assertArr.append(a);
                            continue;
                        }
                        
                        QString leftExpr = match.captured(1).trimmed();
                        QString op = match.captured(2);
                        QString rightValStr = match.captured(3).trimmed();
                        
                        double rightVal = 0.0;
                        if (!SimValueParser::parseSpiceNumber(rightValStr, rightVal)) {
                            a["error"] = "Invalid value: " + rightValStr;
                            a["ok"] = false;
                            allAssertsPassed = false;
                            assertArr.append(a);
                            continue;
                        }
                        
                        MeasureRequest req;
                        QString perr;
                        if (!parseMeasure(leftExpr, &req, &perr)) {
                            a["error"] = perr;
                            a["ok"] = false;
                            allAssertsPassed = false;
                            assertArr.append(a);
                            continue;
                        }
                        
                        QStringList varNames;
                        for (const auto& vn : data.varNames) varNames << QString::fromStdString(vn);
                        const int varIndex = findVarIndex(varNames, req.signalName);
                        if (varIndex < 0) {
                            a["error"] = "Signal not found: " + req.signalName;
                            a["ok"] = false;
                            allAssertsPassed = false;
                            assertArr.append(a);
                            continue;
                        }
                        
                        double actualVal = 0.0;
                        bool valOk = false;
                        if (req.type == MeasureType::At) {
                            QVector<double> xData = QVector<double>(data.x.begin(), data.x.end());
                            const int idx = nearestIndex(xData, req.atTime);
                            if (idx >= 0) {
                                actualVal = (varIndex == 0) ? data.x[idx] : data.y[varIndex - 1][idx];
                                valOk = true;
                            }
                        } else {
                            const QVector<int>& samples = rangeIndices;
                            if (!samples.isEmpty()) {
                                const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                                double minV = vec[samples[0]], maxV = vec[samples[0]], sumV = 0.0, sumSqV = 0.0;
                                for (int idx : samples) {
                                    const double v = vec[idx];
                                    if (v < minV) minV = v;
                                    if (v > maxV) maxV = v;
                                    sumV += v;
                                    sumSqV += v * v;
                                }
                                if (req.type == MeasureType::Min) actualVal = minV;
                                else if (req.type == MeasureType::Max) actualVal = maxV;
                                else if (req.type == MeasureType::Pp) actualVal = (maxV - minV);
                                else if (req.type == MeasureType::Rms) actualVal = std::sqrt(sumSqV / samples.size());
                                else actualVal = (sumV / samples.size());
                                valOk = true;
                            }
                        }
                        
                        if (!valOk) {
                            a["error"] = "No data for measurement";
                            a["ok"] = false;
                            allAssertsPassed = false;
                        } else {
                            a["value"] = actualVal;
                            bool result = false;
                            if (op == "==") result = (std::abs(actualVal - rightVal) < 1e-12);
                            else if (op == "!=") result = (std::abs(actualVal - rightVal) >= 1e-12);
                            else if (op == ">") result = (actualVal > rightVal);
                            else if (op == "<") result = (actualVal < rightVal);
                            else if (op == ">=") result = (actualVal >= rightVal);
                            else if (op == "<=") result = (actualVal <= rightVal);
                            a["ok"] = result;
                            if (!result) allAssertsPassed = false;
                        }
                        assertArr.append(a);
                    }
                    out["assertions"] = assertArr;
                    out["ok"] = out["ok"].toBool() && allAssertsPassed;
                }
            }
        }
        printJsonValue(out);
        _Exit(okForExit ? 0 : 1);
    }

    if (!g_quiet) {
        for (const QString& line : outputs) {
            if (!line.isEmpty()) std::cout << line.toStdString() << std::endl;
        }
    }

    bool allAssertsPassed = true;
    if (okResult && runAssertions) {
        RawData data;
        QString err;
        if (RawDataParser::loadRawAscii(rawPath.toStdString(), &data)) {
            const QVector<int> rangeIndices = filteredIndices(data, tStart, tEnd);
            for (const auto& expr : assertExprs) {
                QRegularExpression re(R"(^(.+?)\s*(==|!=|>=|<=|>|<)\s*(.+)$)");
                auto match = re.match(expr);
                if (!match.hasMatch()) {
                    std::cerr << "Assertion Error: Invalid format: " << expr.toStdString() << std::endl;
                    allAssertsPassed = false;
                    continue;
                }
                
                QString leftExpr = match.captured(1).trimmed();
                QString op = match.captured(2);
                QString rightValStr = match.captured(3).trimmed();
                double rightVal = 0.0;
                SimValueParser::parseSpiceNumber(rightValStr, rightVal);
                
                MeasureRequest req;
                QString perr;
                if (!parseMeasure(leftExpr, &req, &perr)) {
                    std::cerr << "Assertion Error: " << perr.toStdString() << " in " << expr.toStdString() << std::endl;
                    allAssertsPassed = false;
                    continue;
                }
                
                QStringList varNames;
                for (const auto& vn : data.varNames) varNames << QString::fromStdString(vn);
                const int varIndex = findVarIndex(varNames, req.signalName);
                if (varIndex < 0) {
                    std::cerr << "Assertion Error: Signal not found: " << req.signalName.toStdString() << std::endl;
                    allAssertsPassed = false;
                    continue;
                }
                
                double actualVal = 0.0;
                bool valOk = false;
                if (req.type == MeasureType::At) {
                    QVector<double> xData = QVector<double>(data.x.begin(), data.x.end());
                    const int idx = nearestIndex(xData, req.atTime);
                    if (idx >= 0) {
                        actualVal = (varIndex == 0) ? data.x[idx] : data.y[varIndex - 1][idx];
                        valOk = true;
                    }
                } else {
                    const QVector<int>& samples = rangeIndices;
                    if (!samples.isEmpty()) {
                        const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                        double minV = vec[samples[0]], maxV = vec[samples[0]], sumV = 0.0, sumSqV = 0.0;
                        for (int idx : samples) {
                            const double v = vec[idx];
                            if (v < minV) minV = v;
                            if (v > maxV) maxV = v;
                            sumV += v;
                            sumSqV += v * v;
                        }
                        if (req.type == MeasureType::Min) actualVal = minV;
                        else if (req.type == MeasureType::Max) actualVal = maxV;
                        else if (req.type == MeasureType::Pp) actualVal = (maxV - minV);
                        else if (req.type == MeasureType::Rms) actualVal = std::sqrt(sumSqV / samples.size());
                        else actualVal = (sumV / samples.size());
                        valOk = true;
                    }
                }
                
                if (!valOk) {
                    std::cerr << "Assertion Failed: " << expr.toStdString() << " (No data)" << std::endl;
                    allAssertsPassed = false;
                } else {
                    bool result = false;
                    if (op == "==") result = (std::abs(actualVal - rightVal) < 1e-12);
                    else if (op == "!=") result = (std::abs(actualVal - rightVal) >= 1e-12);
                    else if (op == ">") result = (actualVal > rightVal);
                    else if (op == "<") result = (actualVal < rightVal);
                    else if (op == ">=") result = (actualVal >= rightVal);
                    else if (op == "<=") result = (actualVal <= rightVal);
                    
                    if (!result) {
                        std::cerr << "Assertion Failed: " << expr.toStdString() << " (Actual: " << actualVal << ")" << std::endl;
                        allAssertsPassed = false;
                    } else if (!g_quiet) {
                        std::cout << "Assertion Passed: " << expr.toStdString() << " (Value: " << actualVal << ")" << std::endl;
                    }
                }
            }
        }
    }

    if (timedOut) {
        std::cerr << "Error: Simulation timed out." << std::endl;
        if (g_quiet && !parser.isSet("json")) _Exit(1);
        return false;
    }
    if (!errorMsg.isEmpty()) {
        std::cerr << "Error: " << errorMsg.toStdString() << std::endl;
        if (g_quiet && !parser.isSet("json")) _Exit(1);
        return false;
    }

    if (exportRequested) {
        RawData data;
        QString err;
        if (!RawDataParser::loadRawAscii(rawPath.toStdString(), &data)) {
            std::cerr << "Error: " << err.toStdString() << std::endl;
            return false;
        }
        QStringList signalNames = parser.values("signal");
        if (signalNames.isEmpty()) {
            for (int i = 1; i < (int)data.varNames.size(); ++i) {
                signalNames << QString::fromStdString(data.varNames[i]);
            }
        }
        QVector<int> indices;
        for (const auto& sig : signalNames) {
            int idx = -1;
            for (int j = 0; j < (int)data.varNames.size(); ++j) {
                if (QString::fromStdString(data.varNames[j]) == sig) {
                    idx = j;
                    break;
                }
            }
            if (idx < 1) {
                std::cerr << "Error: Signal not found: " << sig.toStdString() << std::endl;
                return false;
            }
            indices << (idx - 1);
        }
        const QVector<int> rangeIndices = filteredIndices(data, tStart, tEnd);
        int baseSignalIndex = -1;
        QString baseError;
        if (!resolveBaseSignalIndex(data, parser.value("base-signal"), &baseSignalIndex, &baseError)) {
            std::cerr << "Error: " << baseError.toStdString() << std::endl;
            return false;
        }

        if (exportMeasures && !measureFormatJson) {
            for (const auto& expr : measureExprs) {
                MeasureRequest req;
                QString perr;
                if (!parseMeasure(expr, &req, &perr)) {
                    std::cerr << expr.toStdString() << " error=" << perr.toStdString() << std::endl;
                    continue;
                }
                QStringList varNames;
                for (const auto& vn : data.varNames) varNames << QString::fromStdString(vn);
                const int varIndex = findVarIndex(varNames, req.signalName);
                if (varIndex < 0) {
                    std::cerr << expr.toStdString() << " error=Signal not found" << std::endl;
                    continue;
                }
                if (req.type == MeasureType::At) {
                    QVector<double> xData = QVector<double>(data.x.begin(), data.x.end());
                    const int idx = nearestIndex(xData, req.atTime);
                    if (idx < 0) {
                        std::cerr << expr.toStdString() << " error=No samples" << std::endl;
                    } else if (varIndex == 0) {
                        std::cout << expr.toStdString() << " = " << data.x[idx] << std::endl;
                    } else {
                        std::cout << expr.toStdString() << " = " << data.y[varIndex - 1][idx] << std::endl;
                    }
                } else {
                    if (rangeIndices.isEmpty()) {
                        std::cerr << expr.toStdString() << " error=No samples in range" << std::endl;
                    } else {
                        const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                        double minVal = vec[rangeIndices[0]];
                        double maxVal = vec[rangeIndices[0]];
                        double sum = 0.0;
                        double sumSq = 0.0;
                        for (int idx : rangeIndices) {
                            const double v = vec[idx];
                            if (v < minVal) minVal = v;
                            if (v > maxVal) maxVal = v;
                            sum += v;
                            sumSq += v * v;
                        }
                        double value = 0.0;
                        if (req.type == MeasureType::Min) value = minVal;
                        else if (req.type == MeasureType::Max) value = maxVal;
                        else if (req.type == MeasureType::Pp) value = (maxVal - minVal);
                        else if (req.type == MeasureType::Rms) value = std::sqrt(sumSq / rangeIndices.size());
                        else value = (sum / rangeIndices.size());
                        std::cout << expr.toStdString() << " = " << value << std::endl;
                    }
                }
            }
        }
        if (exportMeasures && measureFormatJson && exportRaw && exportRawFormat == "csv") {
            QJsonArray measArr;
            for (const auto& expr : measureExprs) {
                MeasureRequest req;
                QString perr;
                QJsonObject m;
                m["expr"] = expr;
                if (!parseMeasure(expr, &req, &perr)) {
                    m["error"] = perr;
                    measArr.append(m);
                    continue;
                }
                QStringList varNames;
                for (const auto& vn : data.varNames) varNames << QString::fromStdString(vn);
                const int varIndex = findVarIndex(varNames, req.signalName);
                if (varIndex < 0) {
                    m["error"] = "Signal not found";
                    measArr.append(m);
                    continue;
                }
                if (req.type == MeasureType::At) {
                    QVector<double> xData = QVector<double>(data.x.begin(), data.x.end());
                    const int idx = nearestIndex(xData, req.atTime);
                    if (idx < 0) m["error"] = "No samples";
                    else if (varIndex == 0) m["value"] = data.x[idx];
                    else m["value"] = data.y[varIndex - 1][idx];
                } else {
                    if (rangeIndices.isEmpty()) {
                        m["error"] = "No samples in range";
                    } else {
                        const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                        double minVal = vec[rangeIndices[0]];
                        double maxVal = vec[rangeIndices[0]];
                        double sum = 0.0;
                        double sumSq = 0.0;
                        for (int idx : rangeIndices) {
                            const double v = vec[idx];
                            if (v < minVal) minVal = v;
                            if (v > maxVal) maxVal = v;
                            sum += v;
                            sumSq += v * v;
                        }
                        if (req.type == MeasureType::Min) m["value"] = minVal;
                        else if (req.type == MeasureType::Max) m["value"] = maxVal;
                        else if (req.type == MeasureType::Pp) m["value"] = (maxVal - minVal);
                        else if (req.type == MeasureType::Rms) m["value"] = std::sqrt(sumSq / rangeIndices.size());
                        else m["value"] = (sum / rangeIndices.size());
                    }
                }
                measArr.append(m);
            }
            QJsonObject out;
            out["measures"] = measArr;
            printJsonValueTo(out, std::cerr);
        }
        if (exportMeasures && measureFormatJson && !exportRaw) {
            QJsonArray measArr;
            for (const auto& expr : measureExprs) {
                MeasureRequest req;
                QString perr;
                QJsonObject m;
                m["expr"] = expr;
                if (!parseMeasure(expr, &req, &perr)) {
                    m["error"] = perr;
                    measArr.append(m);
                    continue;
                }
                QStringList varNames;
                for (const auto& vn : data.varNames) varNames << QString::fromStdString(vn);
                const int varIndex = findVarIndex(varNames, req.signalName);
                if (varIndex < 0) {
                    m["error"] = "Signal not found";
                    measArr.append(m);
                    continue;
                }
                if (req.type == MeasureType::At) {
                    QVector<double> xData = QVector<double>(data.x.begin(), data.x.end());
                    const int idx = nearestIndex(xData, req.atTime);
                    if (idx < 0) m["error"] = "No samples";
                    else if (varIndex == 0) m["value"] = data.x[idx];
                    else m["value"] = data.y[varIndex - 1][idx];
                } else {
                    if (rangeIndices.isEmpty()) {
                        m["error"] = "No samples in range";
                    } else {
                        const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                        double minVal = vec[rangeIndices[0]];
                        double maxVal = vec[rangeIndices[0]];
                        double sum = 0.0;
                        double sumSq = 0.0;
                        for (int idx : rangeIndices) {
                            const double v = vec[idx];
                            if (v < minVal) minVal = v;
                            if (v > maxVal) maxVal = v;
                            sum += v;
                            sumSq += v * v;
                        }
                        if (req.type == MeasureType::Min) m["value"] = minVal;
                        else if (req.type == MeasureType::Max) m["value"] = maxVal;
                        else if (req.type == MeasureType::Pp) m["value"] = (maxVal - minVal);
                        else if (req.type == MeasureType::Rms) m["value"] = std::sqrt(sumSq / rangeIndices.size());
                        else m["value"] = (sum / rangeIndices.size());
                    }
                }
                measArr.append(m);
            }
            QJsonObject out;
            out["measures"] = measArr;
            printJsonValue(out);
        }
        if (exportStats && !exportRaw) {
            const auto stats = computeSignalStats(data, signalNames, indices, rangeIndices);
            for (const auto& s : stats) {
                std::cout << s.name.toStdString()
                          << " min=" << s.min
                          << " max=" << s.max
                          << " avg=" << s.avg
                          << " rms=" << s.rms
                          << std::endl;
            }
        } else if (exportRaw) {
            if (exportRawFormat == "json") {
                QJsonObject out = rawToJson(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex);
                out["file"] = rawPath;
                if (exportStats) {
                    const auto stats = computeSignalStats(data, signalNames, indices, rangeIndices);
                    QJsonArray statArr;
                    for (const auto& s : stats) {
                        QJsonObject st;
                        st["name"] = s.name;
                        st["min"] = s.min;
                        st["max"] = s.max;
                        st["avg"] = s.avg;
                        st["rms"] = s.rms;
                        statArr.append(st);
                    }
                    out["stats"] = statArr;
                }
                if (exportMeasures) {
                    QJsonArray measArr;
                    for (const auto& expr : measureExprs) {
                        MeasureRequest req;
                        QString perr;
                        QJsonObject m;
                        m["expr"] = expr;
                        if (!parseMeasure(expr, &req, &perr)) {
                            m["error"] = perr;
                            measArr.append(m);
                            continue;
                        }
                        QStringList varNames;
                        for (const auto& vn : data.varNames) varNames << QString::fromStdString(vn);
                        const int varIndex = findVarIndex(varNames, req.signalName);
                        if (varIndex < 0) {
                            m["error"] = "Signal not found";
                            measArr.append(m);
                            continue;
                        }
                        if (req.type == MeasureType::At) {
                            QVector<double> xData = QVector<double>(data.x.begin(), data.x.end());
                            const int idx = nearestIndex(xData, req.atTime);
                            if (idx < 0) m["error"] = "No samples";
                            else if (varIndex == 0) m["value"] = data.x[idx];
                            else m["value"] = data.y[varIndex - 1][idx];
                        } else {
                            if (rangeIndices.isEmpty()) {
                                m["error"] = "No samples in range";
                            } else {
                                const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                                double minVal = vec[rangeIndices[0]];
                                double maxVal = vec[rangeIndices[0]];
                                double sum = 0.0;
                                double sumSq = 0.0;
                                for (int idx : rangeIndices) {
                                    const double v = vec[idx];
                                    if (v < minVal) minVal = v;
                                    if (v > maxVal) maxVal = v;
                                    sum += v;
                                    sumSq += v * v;
                                }
                                if (req.type == MeasureType::Min) m["value"] = minVal;
                                else if (req.type == MeasureType::Max) m["value"] = maxVal;
                                else if (req.type == MeasureType::Pp) m["value"] = (maxVal - minVal);
                                else if (req.type == MeasureType::Rms) m["value"] = std::sqrt(sumSq / rangeIndices.size());
                                else m["value"] = (sum / rangeIndices.size());
                            }
                        }
                        measArr.append(m);
                    }
                    out["measures"] = measArr;
                }
                printJsonValue(out);
            } else {
                std::cout << rawToCsv(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex).toStdString();
            }
        }
        _Exit(okForExit ? 0 : 1);
    }
    if (g_exitOnWarning && hasWarnings) {
        if (!g_quiet) {
            std::cerr << "Warning: ngspice reported warnings during simulation." << std::endl;
        }
        if (g_quiet && !parser.isSet("json") && !exportRequested) _Exit(1);
        return false;
    }
    if (g_quiet && !parser.isSet("json") && !exportRequested) { std::_Exit(okResult ? 0 : 1); }
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(okResult ? 0 : 1);
    return okResult; // unreachable
}

bool runNetlistValidate(const QString& filePath, const QCommandLineParser& parser) {
    if (!QFileInfo::exists(filePath)) {
        std::cerr << "Error: Netlist not found: " << filePath.toStdString() << std::endl;
        return false;
    }

    auto& sim = SimulationManager::instance();
    if (!sim.isAvailable()) {
        std::cerr << "Error: Ngspice not available in this build." << std::endl;
        return false;
    }

    QStringList outputs;
    QStringList warnings;
    QString errorMsg;
    QObject::connect(&sim, &SimulationManager::outputReceived, &sim, [&](const QString& msg) {
        const QString trimmed = msg.trimmed();
        outputs << trimmed;
        if (isWarningLine(trimmed)) warnings << trimmed;
    });
    QObject::connect(&sim, &SimulationManager::errorOccurred, &sim, [&](const QString& msg) {
        errorMsg = msg;
    });

    const bool ok = [&]() {
        const bool jsonOut = parser.isSet("json");
        const bool restoreFd = jsonOut;
        const bool silenceFd = g_quiet || jsonOut;
        ScopedFdSilence silence(silenceFd, restoreFd);
        return sim.validateNetlist(filePath, &errorMsg);
    }();
    const bool hasWarnings = !warnings.isEmpty();
    const bool okForExit = ok && errorMsg.isEmpty() && !(g_exitOnWarning && hasWarnings);

    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = filePath;
        out["ok"] = okForExit;
        if (!errorMsg.isEmpty()) out["error"] = errorMsg;
        QJsonArray log;
        for (const QString& line : outputs) log.append(line);
        out["log"] = log;
        if (hasWarnings) {
            QJsonArray warnArr;
            for (const QString& w : warnings) warnArr.append(w);
            out["warnings"] = warnArr;
        }
        out["hasWarnings"] = hasWarnings;
        printJsonValue(out);
        return okForExit;
    }

    if (!g_quiet) {
        for (const QString& line : outputs) {
            if (!line.isEmpty()) std::cout << line.toStdString() << std::endl;
        }
    }
    if (!errorMsg.isEmpty()) {
        std::cerr << "Error: " << errorMsg.toStdString() << std::endl;
        if (g_quiet && !parser.isSet("json")) _Exit(1);
        return false;
    }
    if (g_exitOnWarning && hasWarnings) {
        if (!g_quiet) std::cerr << "Warning: ngspice reported warnings during validation." << std::endl;
        if (g_quiet && !parser.isSet("json")) _Exit(1);
        return false;
    }
    if (ok) std::cout << "Netlist OK" << std::endl;
    if (g_quiet && !parser.isSet("json")) _Exit(okForExit ? 0 : 1);
    return okForExit;
}

bool runNetlistToSchematic(const QString& filePath, const QCommandLineParser& parser) {
    if (!QFileInfo::exists(filePath)) {
        std::cerr << "Error: Netlist not found: " << filePath.toStdString() << std::endl;
        return false;
    }

    QString outPath = parser.value("out");
    if (outPath.isEmpty()) {
        outPath = QFileInfo(filePath).absolutePath() + "/" + QFileInfo(filePath).baseName() + ".flxsch";
    }

    const auto result = NetlistToSchematic::convert(filePath, outPath);

    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = filePath;
        out["output"] = result.outputPath;
        out["ok"] = result.success;
        out["components"] = result.componentCount;
        out["airWires"] = result.airWireCount;
        if (!result.errorMessage.isEmpty()) out["error"] = result.errorMessage;
        printJsonValue(out);
        return result.success;
    }

    if (result.success) {
        if (!g_quiet) {
            std::cout << "Converted " << filePath.toStdString() << " -> " << result.outputPath.toStdString() << std::endl;
            std::cout << "  Components: " << result.componentCount << std::endl;
            std::cout << "  Air wires:  " << result.airWireCount << std::endl;
        }
    } else {
        std::cerr << "Error: " << result.errorMessage.toStdString() << std::endl;
    }
    return result.success;
}

bool runRawInfo(const QString& filePath, const QCommandLineParser& parser) {
    RawData data;
    QString error;
    if (!RawDataParser::loadRawAscii(filePath.toStdString(), &data)) {
        std::cerr << "Error: " << error.toStdString() << std::endl;
        return false;
    }

    if (parser.isSet("summary")) {
        int voltageCount = 0;
        int currentCount = 0;
        for (const auto& vn : data.varNames) {
            QString name = QString::fromStdString(vn);
            if (name.compare("time", Qt::CaseInsensitive) == 0) continue;
            if (name.startsWith("i(", Qt::CaseInsensitive)) currentCount++;
            else voltageCount++;
        }
        if (parser.isSet("json")) {
            QJsonObject out;
            out["file"] = filePath;
            out["variables"] = data.numVariables;
            out["points"] = data.numPoints;
            out["voltages"] = voltageCount;
            out["currents"] = currentCount;
            printJsonValue(out);
            return true;
        }
        std::cout << "File: " << filePath.toStdString() << std::endl;
        std::cout << "Variables: " << data.numVariables << std::endl;
        std::cout << "Points: " << data.numPoints << std::endl;
        std::cout << "Voltages: " << voltageCount << std::endl;
        std::cout << "Currents: " << currentCount << std::endl;
        return true;
    }

    int baseSignalIndex = -1;
    const QString baseSignalName = parser.value("base-signal").trimmed();
    if (!baseSignalName.isEmpty()) {
        int idx = -1;
        for (int i = 0; i < (int)data.varNames.size(); ++i) {
            if (QString::fromStdString(data.varNames[i]).compare(baseSignalName, Qt::CaseInsensitive) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 1) {
            std::cerr << "Error: Base signal not found (or invalid): " << baseSignalName.toStdString() << std::endl;
            return false;
        }
        baseSignalIndex = idx - 1;
    }

    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = filePath;
        out["variables"] = data.numVariables;
        out["points"] = data.numPoints;
        QJsonArray names;
        for (const auto& vn : data.varNames) names.append(QString::fromStdString(vn));
        out["varNames"] = names;
        printJsonValue(out);
        return true;
    }

    std::cout << "File: " << filePath.toStdString() << std::endl;
    std::cout << "Variables: " << data.numVariables << std::endl;
    std::cout << "Points: " << data.numPoints << std::endl;
    for (const auto& vn : data.varNames) {
        std::cout << "  " << vn << std::endl;
    }
    return true;
}

bool runViewRaw(const QString& filePath, const QCommandLineParser& parser) {
    RawData data;
    if (!RawDataParser::loadRawAscii(filePath.toStdString(), &data)) {
        std::cerr << "Error: Failed to load raw file: " << filePath.toStdString() << std::endl;
        return false;
    }

    // We don't want to use 'offscreen' for the viewer!
    // This is already handled in main() before QApplication init.

    QMainWindow* window = new QMainWindow();
    window->setWindowTitle(QString("VioSpice Waveform Viewer - %1").arg(QFileInfo(filePath).fileName()));
    window->resize(1000, 600);

    WaveformViewer* viewer = new WaveformViewer(window);
    window->setCentralWidget(viewer);

    QVector<double> time(data.x.begin(), data.x.end());

    viewer->beginBatchUpdate();
    for (size_t i = 1; i < data.varNames.size(); ++i) {
        if (i - 1 < data.y.size()) {
            QVector<double> values(data.y[i - 1].begin(), data.y[i - 1].end());
            QString name = QString::fromStdString(data.varNames[i]);
            viewer->addSignal(name, time, values);
            viewer->setSignalChecked(name, true);
        }
    }
    viewer->endBatchUpdate();
    viewer->zoomFit();

    window->show();
    
    // Since we are in a CLI that usually doesn't have an event loop running,
    // we use qApp->exec(). However, main() might already be prepared for this.
    return true; // We will call app.exec() in main()
}

bool runViewOsc(const QString& filePath, const QCommandLineParser& parser) {
    RawData data;
    if (!RawDataParser::loadRawAscii(filePath.toStdString(), &data)) {
        std::cerr << "Error: Failed to load raw file: " << filePath.toStdString() << std::endl;
        return false;
    }

    QMainWindow* window = new QMainWindow();
    window->setWindowTitle(QString("VioSpice Analog Oscilloscope - %1").arg(QFileInfo(filePath).fileName()));
    window->resize(1200, 800);

    // SimulationPanel(scene, netManager, projectDir)
    SimulationPanel* panel = new SimulationPanel(nullptr, nullptr, "");
    window->setCentralWidget(panel);

    panel->plotResultsFromRaw(filePath);
    window->show();
    
    // We will call app.exec() in main()
    return true;
}

bool runRawExport(const QString& filePath, const QCommandLineParser& parser) {
    RawData data;
    QString error;
    if (!RawDataParser::loadRawAscii(filePath.toStdString(), &data)) {
        std::cerr << "Error: " << error.toStdString() << std::endl;
        return false;
    }

    double tStart = std::numeric_limits<double>::quiet_NaN();
    double tEnd = std::numeric_limits<double>::quiet_NaN();
    QString rangeError;
    if (!parseRangeOption(parser.value("range"), &tStart, &tEnd, &rangeError)) {
        std::cerr << "Error: " << rangeError.toStdString() << std::endl;
        return false;
    }

    QStringList signalNames = parser.values("signal");
    const QString signalRegex = parser.value("signal-regex").trimmed();
    if (signalNames.isEmpty()) {
        for (int i = 1; i < (int)data.varNames.size(); ++i) {
            signalNames << QString::fromStdString(data.varNames[i]);
        }
    }
    if (!signalRegex.isEmpty()) {
        QRegularExpression re(signalRegex);
        if (!re.isValid()) {
            std::cerr << "Error: Invalid signal-regex: " << signalRegex.toStdString() << std::endl;
            return false;
        }
        QStringList filtered;
        for (const auto& name : signalNames) {
            if (re.match(name).hasMatch()) filtered << name;
        }
        signalNames = filtered;
    }
    if (signalNames.isEmpty()) {
        std::cerr << "Error: No signals matched." << std::endl;
        return false;
    }

    QVector<int> indices;
    for (const auto& sig : signalNames) {
        int idx = -1;
        for (int i = 0; i < (int)data.varNames.size(); ++i) {
            if (QString::fromStdString(data.varNames[i]) == sig) {
                idx = i;
                break;
            }
        }
        if (idx < 1) {
            std::cerr << "Error: Signal not found: " << sig.toStdString() << std::endl;
            return false;
        }
        indices << (idx - 1);
    }
    int baseSignalIndex = -1;
    QString baseError;
    if (!resolveBaseSignalIndex(data, parser.value("base-signal"), &baseSignalIndex, &baseError)) {
        std::cerr << "Error: " << baseError.toStdString() << std::endl;
        return false;
    }

    const QString format = parser.value("format").trimmed().toLower();
    bool ok = false;
    const int maxPoints = parser.value("max-points").toInt(&ok);
    const int maxPointsValue = ok ? maxPoints : 0;
    if (format == "parquet") {
        const QString outPath = parser.value("out").trimmed();
        if (outPath.isEmpty()) {
            std::cerr << "Error: --out is required for parquet export." << std::endl;
            return false;
        }
        const QString csvData = rawToCsv(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex);
        QTemporaryFile temp(QDir::tempPath() + "/viospice_raw_XXXXXX.csv");
        if (!temp.open()) {
            std::cerr << "Error: Failed to create temp CSV for parquet export." << std::endl;
            return false;
        }
        temp.write(csvData.toUtf8());
        temp.flush();

        QProcess proc;
        QStringList args;
        const QString script = QStringLiteral(
            "import sys\n"
            "try:\n"
            "    import pyarrow.csv as csv\n"
            "    import pyarrow.parquet as pq\n"
            "except Exception as e:\n"
            "    sys.stderr.write('pyarrow import failed: %s\\n' % e)\n"
            "    sys.exit(2)\n"
            "table = csv.read_csv(sys.argv[1])\n"
            "pq.write_table(table, sys.argv[2])\n"
        );
        args << "-c" << script << temp.fileName() << outPath;
        
        QString pythonExe = FluxScriptManager::getPythonExecutable();
        proc.start(pythonExe, args);
        if (!proc.waitForFinished(60000)) {
            std::cerr << "Error: parquet export timed out." << std::endl;
            return false;
        }
        if (proc.exitCode() != 0) {
            const QByteArray err = proc.readAllStandardError();
            std::cerr << "Error: parquet export failed. " << err.toStdString()
                      << "Hint: install pyarrow in a venv: " << pythonExe.toStdString() << " -m venv .venv && . .venv/bin/activate && pip install pyarrow"
                      << std::endl;
            return false;
        }
        if (parser.isSet("json")) {
            QJsonObject out;
            out["file"] = outPath;
            out["format"] = "parquet";
            printJsonValue(out);
        } else {
            std::cout << outPath.toStdString() << std::endl;
        }
        return true;
    }
    if (format == "json") {
        QJsonObject out = rawToJson(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex);
        out["file"] = filePath;
        printJsonValue(out);
        return true;
    }

    // Default CSV
    std::cout << rawToCsv(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex).toStdString();
    return true;
}

bool runVerilogInspect(const QString& filePath, const QCommandLineParser& parser) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "Error: Cannot open Verilog file: " << filePath.toStdString() << std::endl;
        return false;
    }
    QString source = QString::fromUtf8(file.readAll());
    file.close();

    QString error;
    // We try to extract ports for all modules? Or just the one with moduleName?
    // SlangManager::extractPorts takes a moduleName. If empty, it might fail or we need a way to list them.
    // For now, let's assume 'top' or allow user to specify via --module
    QString moduleName = parser.value("module");
    if (moduleName.isEmpty()) {
        // Simple heuristic: find first 'module NAME'
        static const QRegularExpression modRe(R"(\bmodule\s+(\w+))");
        auto match = modRe.match(source);
        if (match.hasMatch()) moduleName = match.captured(1);
    }

    auto ports = SlangManager::instance().extractPorts(source, moduleName, &error);
    if (!error.isEmpty() && ports.isEmpty()) {
        std::cerr << "Slang Error: " << error.toStdString() << std::endl;
        return false;
    }

    if (parser.isSet("json")) {
        QJsonObject root;
        root["file"] = filePath;
        root["module"] = moduleName;
        QJsonArray portArray;
        for (const auto& p : ports) {
            QJsonObject po;
            po["name"] = p.name;
            po["width"] = p.width;
            po["direction"] = p.isInput ? "input" : "output";
            portArray.append(po);
        }
        root["ports"] = portArray;
        if (!error.isEmpty()) root["warnings"] = error;
        printJsonValue(root);
    } else {
        std::cout << "File: " << filePath.toStdString() << "\n";
        std::cout << "Module: " << moduleName.toStdString() << "\n";
        std::cout << "Ports:\n";
        for (const auto& p : ports) {
            std::cout << "  " << (p.isInput ? "input " : "output") << " [" << p.width << "] " << p.name.toStdString() << "\n";
        }
        if (!error.isEmpty()) std::cout << "\nWarnings/Errors:\n" << error.toStdString() << "\n";
    }

    return true;
}

void printSchema(const QString& command) {
    QJsonObject root;
    root["command"] = command;

    auto setSchema = [&](const QJsonObject& input, const QJsonObject& output) {
        root["input"] = input;
        root["output"] = output;
    };

    if (command == "schematic-query") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}},
            QJsonObject{
                {"file", "string"},
                {"pageSize", "string"},
                {"components", "array[component]"},
                {"nets", "array[net]"}
            }
        );
    } else if (command == "schematic-netlist") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}, {"options", QJsonObject{{"format", "spice|json"}, {"analysis", "op|tran|ac"}, {"step", "string"}, {"stop", "string"}, {"out", "file"}}}},
            QJsonObject{{"netlist", "string (spice or json)"}}
        );
    } else if (command == "schematic-render") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch", "out.png"}}, {"options", QJsonObject{{"transparent", "bool"}, {"json", "bool"}, {"scale", "number"}}}},
            QJsonObject{{"file", "string"}, {"output", "string"}, {"width", "int"}, {"height", "int"}, {"scale", "number"}, {"transparent", "bool"}, {"bounds", "rect"}}
        );
    } else if (command == "schematic-bom") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}},
            QJsonObject{{"file", "string"}, {"components", "array[component]"}, {"groups", "array[group]"}}
        );
    } else if (command == "schematic-validate") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}},
            QJsonObject{{"file", "string"}, {"erc", "array[erc_issue]"}, {"preflight", "array[string]"}, {"summary", "object"}}
        );
    } else if (command == "schematic-diff") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"a.flxsch", "b.flxsch"}}},
            QJsonObject{{"a", "string"}, {"b", "string"}, {"components", "object{added,removed,changed}"}}
        );
    } else if (command == "schematic-transform") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}, {"options", QJsonObject{{"rename-net", "old=new (repeatable)"}, {"normalize-value", "old=new (repeatable)"}, {"prefix-ref", "old=new"}}}},
            QJsonObject{{"file", "string"}, {"renamedNets", "int"}, {"normalizedValues", "int"}, {"updatedRefs", "int"}}
        );
    } else if (command == "schematic-probe") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}, {"options", QJsonObject{{"list", "bool"}, {"add", "signal (repeatable)"}, {"auto", "bool"}}}},
            QJsonObject{{"file", "string"}, {"signals", "array[string]"}, {"voltages", "array[string]"}, {"currents", "array[string]"}, {"probes", "array[string]"}}
        );
    } else if (command == "netlist-compare") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch", "external.net"}}, {"options", QJsonObject{{"analysis", "op|tran|ac"}, {"step", "string"}, {"stop", "string"}}}},
            QJsonObject{{"schematic", "string"}, {"external", "string"}, {"schematicLineCount", "int"}, {"externalLineCount", "int"}, {"differences", "array[{line,schematicCount,externalCount}]"}, {"summary", "object"}}
        );
    } else if (command == "netlist-validate") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.cir"}}, {"options", QJsonObject{{"json", "bool"}}}},
            QJsonObject{{"file", "string"}, {"ok", "bool"}, {"error", "string"}, {"log", "array[string]"}}
        );
    } else if (command == "netlist-to-schematic") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.cir"}}, {"options", QJsonObject{{"json", "bool"}, {"out", "string"}, {"quiet", "bool"}}}},
            QJsonObject{{"file", "string"}, {"output", "string"}, {"components", "int"}, {"airWires", "int"}, {"ok", "bool"}, {"error", "string"}}
        );
    } else if (command == "netlist-run") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.cir|file.flxsch"}}, {"options", QJsonObject{{"json", "bool"}, {"timeout", "string"}, {"analysis", "op|tran|ac"}, {"step", "string"}, {"stop", "string"}, {"export-raw", "csv|json"}, {"signal", "name (repeatable)"}, {"max-points", "int"}, {"base-signal", "name"}, {"stats", "bool"}, {"range", "t0:t1"}, {"measure", "expr (repeatable)"}, {"assert", "expr (repeatable)"}}}},
            QJsonObject{{"file", "string"}, {"ok", "bool"}, {"timeout", "bool"}, {"error", "string"}, {"log", "array[string]"}, {"rawPath", "string"}}
        );
    } else if (command == "raw-info") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.raw"}}, {"options", QJsonObject{{"json", "bool"}, {"summary", "bool"}}}},
            QJsonObject{{"file", "string"}, {"variables", "int"}, {"points", "int"}, {"varNames", "array[string]"}, {"voltages", "int"}, {"currents", "int"}}
        );
    } else if (command == "raw-export") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.raw"}}, {"options", QJsonObject{{"signal", "name (repeatable)"}, {"signal-regex", "pattern"}, {"format", "csv|json|parquet"}, {"max-points", "int"}, {"base-signal", "name"}, {"range", "t0:t1"}, {"out", "file (parquet)"}}}},
            QJsonObject{{"file", "string"}, {"x", "array[number]"}, {"signals", "array[{name,values}]"}}
        );
    } else if (command == "raw-stats") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.raw"}}, {"options", QJsonObject{{"signal", "name (repeatable)"}, {"range", "t0:t1"}}}},
            QJsonObject{{"file", "string"}, {"stats", "array[object]"}}
        );
    } else if (command == "symbol-query") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.viosym"}}},
            QJsonObject{{"file", "string"}, {"name", "string"}, {"modelName", "string"}, {"pins", "array[pin]"}, {"boundingRect", "rect"}}
        );
    } else if (command == "symbol-validate") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.viosym"}}},
            QJsonObject{{"file", "string"}, {"name", "string"}, {"pinCount", "int"}, {"issues", "array[issue]"}, {"summary", "object"}}
        );
    } else if (command == "symbol-render") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.viosym", "out.png"}}, {"options", QJsonObject{{"transparent", "bool"}, {"json", "bool"}, {"scale", "number"}}}},
            QJsonObject{{"file", "string"}, {"output", "string"}, {"transparent", "bool"}, {"scale", "number"}}
        );
    } else if (command == "symbol-list") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"folder|library.sclib"}}},
            QJsonObject{{"path", "string"}, {"symbols", "array[{name,source}]"}}
        );
    } else if (command == "symbol-export") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"symbolName", "library.sclib", "out.viosym"}}},
            QJsonObject{{"ok", "bool"}, {"output", "string"}}
        );
    } else if (command == "symbol-import") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"input.asy|input.kicad_sym", "out.viosym|out.sclib"}}, {"options", QJsonObject{{"name", "symbolName (for KiCad)"}}}},
            QJsonObject{{"input", "string"}, {"output", "string"}, {"name", "string"}, {"footprint", "string"}}
        );
    } else if (command == "library-index") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"folder"}}, {"options", QJsonObject{{"include-comments", "bool"}}}},
            QJsonObject{{"root", "string"}, {"symbols", "array[{name,path,type}]"}, {"models", "array[{path,type,subckts,models}]"}, {"modelIndex", "object"}}
        );
    } else {
        root["error"] = "No schema available for this command.";
    }

    printJsonValue(root);
}

bool runPluginPack(const QStringList& args) {
    if (args.size() < 4) {
        std::cerr << "Usage: viora plugin-pack <manifest.json> <artifact-file> <output.fluxplugin>" << std::endl;
        return false;
    }

    const QString manifestPath = args.at(1);
    const QString artifactPath = args.at(2);
    const QString outputPath = args.at(3);

    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read manifest: " << manifestPath.toStdString() << std::endl;
        return false;
    }
    const QByteArray manifestBytes = manifestFile.readAll();
    manifestFile.close();

    QJsonParseError parseError;
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject()) {
        std::cerr << "Error: Invalid manifest JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }
    const QJsonObject manifest = manifestDoc.object();
    const QString pluginId = manifest.value("id").toString().trimmed();
    const QString version = manifest.value("version").toString().trimmed();
    if (pluginId.isEmpty() || version.isEmpty()) {
        std::cerr << "Error: manifest must contain non-empty id and version fields." << std::endl;
        return false;
    }

    QFile artifactFile(artifactPath);
    if (!artifactFile.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read artifact: " << artifactPath.toStdString() << std::endl;
        return false;
    }
    const QByteArray artifactBytes = artifactFile.readAll();
    artifactFile.close();

    const QString artifactSha = sha256Hex(artifactBytes);
    const QString artifactName = QFileInfo(artifactPath).fileName();

    QJsonObject payload;
    payload["format"] = "fluxplugin-v1";
    payload["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    payload["manifest"] = manifest;

    QJsonObject artifact;
    artifact["name"] = artifactName;
    artifact["sizeBytes"] = static_cast<qint64>(artifactBytes.size());
    artifact["sha256"] = artifactSha;
    artifact["contentBase64"] = QString::fromLatin1(artifactBytes.toBase64());
    payload["artifact"] = artifact;

    const QJsonDocument outDoc(payload);
    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "Error: Cannot write output package: " << outputPath.toStdString() << std::endl;
        return false;
    }
    outFile.write(outDoc.toJson(QJsonDocument::Indented));
    outFile.close();

    if (!g_quiet) {
        std::cout << "Packed plugin:" << std::endl;
        std::cout << "  ID: " << pluginId.toStdString() << std::endl;
        std::cout << "  Version: " << version.toStdString() << std::endl;
        std::cout << "  Artifact: " << artifactName.toStdString() << " (" << artifactBytes.size() << " bytes)" << std::endl;
        std::cout << "  SHA-256: " << artifactSha.toStdString() << std::endl;
        std::cout << "  Output: " << outputPath.toStdString() << std::endl;
    }
    return true;
}

bool runPluginInspect(const QStringList& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: viora plugin-inspect <package.fluxplugin>" << std::endl;
        return false;
    }
    const QString packagePath = args.at(1);

    QFile packageFile(packagePath);
    if (!packageFile.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read package: " << packagePath.toStdString() << std::endl;
        return false;
    }
    const QByteArray bytes = packageFile.readAll();
    packageFile.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Error: Invalid package JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }

    const QJsonObject root = doc.object();
    const QString format = root.value("format").toString();
    const QJsonObject manifest = root.value("manifest").toObject();
    const QJsonObject artifact = root.value("artifact").toObject();

    const QString pluginId = manifest.value("id").toString();
    const QString version = manifest.value("version").toString();
    const QString artifactName = artifact.value("name").toString();
    const qint64 sizeBytes = static_cast<qint64>(artifact.value("sizeBytes").toDouble(0.0));
    const QString expectedSha = artifact.value("sha256").toString().toLower();
    const QByteArray content = QByteArray::fromBase64(artifact.value("contentBase64").toString().toLatin1());
    const QString actualSha = sha256Hex(content);

    if (!g_quiet) {
        std::cout << "Package: " << packagePath.toStdString() << std::endl;
        std::cout << "  Format: " << format.toStdString() << std::endl;
        std::cout << "  Plugin ID: " << pluginId.toStdString() << std::endl;
        std::cout << "  Version: " << version.toStdString() << std::endl;
        std::cout << "  Artifact: " << artifactName.toStdString() << std::endl;
        std::cout << "  Declared Size: " << sizeBytes << std::endl;
        std::cout << "  Extracted Size: " << content.size() << std::endl;
        std::cout << "  Declared SHA-256: " << expectedSha.toStdString() << std::endl;
        std::cout << "  Actual SHA-256:   " << actualSha.toStdString() << std::endl;
    }

    const bool sizeOk = (sizeBytes == content.size());
    const bool shaOk = (!expectedSha.isEmpty() && expectedSha == actualSha);
    const bool manifestOk = (!pluginId.trimmed().isEmpty() && !version.trimmed().isEmpty());

    if (!g_quiet) {
        std::cout << "Validation:" << std::endl;
        std::cout << "  Manifest fields: " << (manifestOk ? "OK" : "FAIL") << std::endl;
        std::cout << "  Artifact size:   " << (sizeOk ? "OK" : "FAIL") << std::endl;
        std::cout << "  Artifact sha256: " << (shaOk ? "OK" : "FAIL") << std::endl;
    }

    return manifestOk && sizeOk && shaOk;
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
} // namespace

static void printGeneralHelp() {
    std::cout << "Usage: viora <command> [file] [options]\n\n";
    std::cout << "Common commands:\n";
    std::cout << "  flux              FluxScript SPICE integration\n";
    std::cout << "  schematic-query <file.flxsch>\n";
    std::cout << "  schematic-netlist <file.flxsch> [--analysis tran|ac|op] [--step <s>] [--stop <s>]\n";
    std::cout << "  netlist-run <file.cir|file.flxsch> [--analysis tran|ac|op] [--export-raw csv|json]\n";
    std::cout << "  netlist-to-schematic <file.cir> [--out <file.flxsch>]\n";
    std::cout << "  view <file.raw> [--type plot|osc]\n";
    std::cout << "  raw-info <file.raw> [--summary --json]\n";
    std::cout << "  raw-export <file.raw> [--format csv|json|parquet] [--out <file>]\n";
    std::cout << "  symbol-render <file.viosym> <out.png>\n";
    std::cout << "  symbol-import <input.asy|input.kicad_sym> <out.viosym|out.sclib> [--name <symbol>]\n";
    std::cout << "  symbol-export <symbolName> <library.sclib> <out.viosym>\n";
    std::cout << "  symbol-list <folder|library.sclib>\n";
    std::cout << "  symbol-validate <file.viosym>\n";
    std::cout << "  symbol-from-subckt <input.cir|lib> <out_dir> [--name <subckt>]\n";
    std::cout << "  library-to-symbols <input_path> <out_dir> [--recursive]\n";
    std::cout << "  library-auto-convert <input_path> <out_dir> [--mapping <mapping.json>] [--recursive]\n";
    std::cout << "  screenshot [--name <name>] [--output <file.png>]\n";
    std::cout << "  gui <subcommand> [<args>]\n";
    std::cout << "\nTips:\n";
    std::cout << "  Use \"viora help <command>\" for command-specific help.\n";
    std::cout << "  Use --json for machine-readable output.\n";
}

static void printCommandHelp(const QString& command) {
    if (command == "extension") {
        std::cout << "viora extension - Manage FluxScript Extensions\n";
        std::cout << "\n";
        std::cout << "Usage: viora extension <action> [name|dir]\n";
        std::cout << "\n";
        std::cout << "Actions:\n";
        std::cout << "  init <name>         Scaffold a new extension\n";
        std::cout << "  validate <dir>      Validate manifest and compile check\n";
        std::cout << "  install <dir>       Install extension to config dir\n";
        return;
    }

    if (command == "flux") {
        std::cout << "viora flux - FluxScript SPICE Integration\n";
        std::cout << "\n";
        std::cout << "Usage: viora flux <subcommand> [options] [file.flux]\n";
        std::cout << "\n";
        std::cout << "Subcommands:\n";
        std::cout << "  run <file.flux>     Compile and run FluxScript file\n";
        std::cout << "  compile <file.flux> Generate SPICE netlist\n";
        std::cout << "  eval <expression>   Evaluate FluxScript expression\n";
        std::cout << "  repl                Interactive REPL mode\n";
        std::cout << "\n";
        std::cout << "Examples:\n";
        std::cout << "  viora flux run circuit.flux\n";
        std::cout << "  viora flux compile circuit.flux -o circuit.cir\n";
        std::cout << "  viora flux eval \"sin(pi/2)\"\n";
        std::cout << "  viora flux repl\n";
        return;
    }

    if (command == "netlist-run") {
        std::cout << "netlist-run <file.cir|file.flxsch>\n";
        std::cout << "  --analysis tran|ac|op  --step <s>  --stop <s>\n";
        std::cout << "  --export-raw csv|json  --signal <name> (repeatable)\n";
        std::cout << "  --max-points <n>  --base-signal <name>  --range t0:t1\n";
        std::cout << "  --stats  --measure <expr> (repeatable)  --measure-format text|json\n";
        std::cout << "  --assert <expr> (repeatable)  --compat  --quiet  --json  --timeout <10s>\n";
        return;
    }
    if (command == "raw-export") {
        std::cout << "raw-export <file.raw>\n";
        std::cout << "  --format csv|json|parquet  --out <file> (parquet)\n";
        std::cout << "  --signal <name> (repeatable)  --signal-regex <pattern>\n";
        std::cout << "  --max-points <n>  --base-signal <name>  --range t0:t1\n";
        return;
    }
    if (command == "raw-stats") {
        std::cout << "raw-stats <file.raw>\n";
        std::cout << "  --signal <name> (repeatable)  --range t0:t1\n";
        return;
    }
    if (command == "view") {
        std::cout << "view <file.raw>\n";
        std::cout << "  --type plot|osc        Viewer type: standard plot or hardware-realistic oscilloscope (default: plot)\n";
        return;
    }
    if (command == "raw-info") {
        std::cout << "raw-info <file.raw>\n";
        std::cout << "  --summary  --json\n";
        return;
    }
    if (command == "schematic-netlist") {
        std::cout << "schematic-netlist <file.flxsch>\n";
        std::cout << "  --analysis tran|ac|op  --step <s>  --stop <s>\n";
        std::cout << "  --format spice|json  --out <file>\n";
        return;
    }
    if (command == "screenshot") {
        std::cout << "screenshot [options]\n";
        std::cout << "\n";
        std::cout << "Capture screenshots of open VioSpice windows.\n";
        std::cout << "Requires a running VioSpice GUI instance.\n";
        std::cout << "\n";
        std::cout << "Options:\n";
        std::cout << "  --name <name>        Window class name or title substring\n";
        std::cout << "  --output <file>      Output file path\n";
        std::cout << "  --format <fmt>       Output format: PNG, JPG, BMP (default: PNG)\n";
        std::cout << "  --scale <n>          Device pixel scale factor (default: 1.0)\n";
        std::cout << "  --region <x,y,w,h>  Capture a specific region of the widget\n";
        std::cout << "  --clipboard          Copy screenshot to clipboard\n";
        std::cout << "  --include-hidden     Include hidden/docked widgets\n";
        std::cout << "  --list-children      List child widgets of a parent window\n";
        std::cout << "  --watch              Continuously capture frames\n";
        std::cout << "  --interval <ms>      Interval for --watch mode (default: 1000)\n";
        std::cout << "  --output-dir <dir>   Directory for --watch mode frames\n";
        std::cout << "  --json               Machine-readable output\n";
        std::cout << "\n";
        std::cout << "Examples:\n";
        std::cout << "  viora screenshot                                        List open windows\n";
        std::cout << "  viora screenshot --name SchematicEditor                 Capture window\n";
        std::cout << "  viora screenshot --name PCB --format JPG --scale 2.0   Capture as JPG at 2x\n";
        std::cout << "  viora screenshot --name WaveformViewer --region 0,0,500,300  Region capture\n";
        std::cout << "  viora screenshot --name SchematicEditor --list-children     List dock panels\n";
        std::cout << "  viora screenshot --name Oscilloscope --include-hidden      Find hidden docks\n";
        std::cout << "  viora screenshot --watch --name Schematic --interval 500 --output-dir /tmp/frames\n";
        return;
    }
    if (command == "gui") {
        std::cout << "gui <subcommand> [options]\n";
        std::cout << "\n";
        std::cout << "Control the running VioSpice GUI remotely.\n";
        std::cout << "Requires a running VioSpice GUI instance.\n";
        std::cout << "\n";
        std::cout << "Subcommands:\n";
        std::cout << "  list-buttons                  List interactive elements\n";
        std::cout << "  click <label-or-name>         Click a button or trigger an action\n";
        std::cout << "  type <field> <text>           Type text into an input field\n";
        std::cout << "  menu <action-text>            Trigger a menu action\n";
        std::cout << "\n";
        std::cout << "Options:\n";
        std::cout << "  --window <name>    Target window (default: SchematicEditor)\n";
        std::cout << "  --type <type>      Filter by widget type (QPushButton, QToolButton, QAction, QLineEdit)\n";
        std::cout << "  --parent <name>    Filter by parent toolbar/objectName\n";
        std::cout << "  --append           Append text instead of replacing (for 'type')\n";
        std::cout << "  --json             Machine-readable output\n";
        std::cout << "\n";
        std::cout << "Examples:\n";
        std::cout << "  viora gui list-buttons                                  List all buttons\n";
        std::cout << "  viora gui list-buttons --type QToolButton --parent MainToolbar\n";
        std::cout << "  viora gui click \"Run Simulation\"                       Click a button\n";
        std::cout << "  viora gui menu \"Export as PDF\"                          Trigger menu action\n";
        std::cout << "  viora gui type ProjectSearch \"my project\"              Type in search field\n";
        return;
    }
    if (command == "generate-report") {
        std::cout << "generate-report <file.flxsch> <out.html>\n";
        std::cout << "  --title <title>  --author <name>\n";
        std::cout << "  --raw-file <file.raw>  --schematic-png <image.png>\n";
        std::cout << "  --no-schematic  --no-waveforms  --no-measurements  --no-netlist\n";
        std::cout << "  --max-points <n>  --json\n";
        return;
    }
    if (command == "share") {
        std::cout << "share <file.flxsch>\n";
        std::cout << "  --title <title>  --description <desc>\n";
        std::cout << "  --upload  --copy  --server <url>\n";
        std::cout << "  --json\n";
        return;
    }
    if (command == "schematic-query") {
        std::cout << "schematic-query <file.flxsch>\n";
        std::cout << "  --json\n";
        return;
    }
    if (command == "symbol-render") {
        std::cout << "symbol-render <file.viosym> <out.png>\n";
        std::cout << "  --scale <n>  --transparent  --json\n";
        return;
    }
    if (command == "item-render") {
        std::cout << "item-render <file.json> <out.png>\n";
        std::cout << "  Render an instantiated schematic item (like an instrument) from JSON to PNG.\n";
        std::cout << "  --scale <n>  --transparent  --json\n";
        return;
    }
    if (command == "symbol-validate") {
        std::cout << "symbol-validate <file.viosym>\n";
        std::cout << "  --json\n";
        return;
    }
    if (command == "symbol-from-subckt") {
        std::cout << "symbol-from-subckt <input.cir|lib> <out_dir>\n";
        std::cout << "  --name <subckt>        Generate symbol only for specific subcircuit\n";
        std::cout << "  --symbol-type <t>      Symbol type: op, comparator, regulator, triac, scr, diode, zener, led, logic_gate_3pin, gate_and, gate_or, etc.\n";
        std::cout << "  --json\n";
        return;
    }
    if (command == "library-to-symbols") {
        std::cout << "library-to-symbols <input_path> <out_dir>\n";
        std::cout << "  --recursive            Scan subdirectories for library files\n";
        std::cout << "  --symbol-type <t>      Symbol geometry type: ic, op, npn, pnp, nmos, pmos, diode\n";
        std::cout << "  --json\n";
        std::cout << "\nNote: This command creates subfolders for each library for better organization.\n";
        return;
    }
    if (command == "library-auto-convert") {
        std::cout << "library-auto-convert <input_path> <out_dir> --mapping <mapping.json>\n";
        std::cout << "  --mapping <file.json>  JSON file containing matching rules for symbols\n";
        std::cout << "  --recursive            Scan subdirectories for library files\n";
        std::cout << "\nThis command uses the mapping JSON to automatically assign the best high-fidelity\n";
        std::cout << "symbol shape to each component in the SPICE library.\n";
        return;
    }
    printGeneralHelp();
}

bool runRawStats(const QString& filePath, const QCommandLineParser& parser) {
    RawData data;
    if (!RawDataParser::loadRawAscii(filePath.toStdString(), &data)) {
        std::cerr << "Error: Failed to load raw file." << std::endl;
        return false;
    }

    double tStart = std::numeric_limits<double>::quiet_NaN();
    double tEnd = std::numeric_limits<double>::quiet_NaN();
    QString rangeError;
    if (!parseRangeOption(parser.value("range"), &tStart, &tEnd, &rangeError)) {
        std::cerr << "Error: " << rangeError.toStdString() << std::endl;
        return false;
    }

    QStringList signalNames = parser.values("signal");
    if (signalNames.isEmpty()) {
        for (int i = 1; i < (int)data.varNames.size(); ++i) {
            signalNames << QString::fromStdString(data.varNames[i]);
        }
    }

    QJsonObject out;
    out["file"] = filePath;
    QJsonArray results;

    for (const auto& sig : signalNames) {
        int idx = -1;
        for (int i = 0; i < (int)data.varNames.size(); ++i) {
            if (QString::fromStdString(data.varNames[i]) == sig) {
                idx = i;
                break;
            }
        }
        if (idx < 1) continue;
        
        const auto& values = data.y[idx - 1];
        double minVal = std::numeric_limits<double>::max();
        double maxVal = std::numeric_limits<double>::lowest();
        double sum = 0;
        double sumSq = 0;
        int count = 0;

        for (size_t i = 0; i < data.x.size(); ++i) {
            double t = data.x[i];
            if (!std::isnan(tStart) && t < tStart) continue;
            if (!std::isnan(tEnd) && t > tEnd) continue;

            double v = values[i];
            minVal = std::min(minVal, v);
            maxVal = std::max(maxVal, v);
            sum += v;
            sumSq += v * v;
            count++;
        }

        if (count > 0) {
            QJsonObject s;
            s["signal"] = sig;
            s["min"] = minVal;
            s["max"] = maxVal;
            s["avg"] = sum / count;
            s["rms"] = std::sqrt(sumSq / count);
            s["points"] = count;
            results.append(s);
        }
    }
    out["stats"] = results;
    printJsonValue(out);
    return true;
}

int main(int argc, char *argv[]) {
    // Handle --help and --version before QApplication (avoids slow offscreen init)
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

    // Some GUI classes like QGraphicsScene and QColor require QApplication
    // We run with offscreen platform to keep it CLI-friendly, unless we want to 'view'
    bool isView = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "view") {
            isView = true;
            break;
        }
    }
#ifdef Q_OS_LINUX
    // Linux: use offscreen platform so CLI works without a display
    if (!isView) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#elif defined(Q_OS_WIN)
    // Windows: keep default platform (qwindows.dll); offscreen plugin is often missing
    if (!isView) {
        qputenv("QT_QPA_PLATFORM", "windows");
    }
#else
    if (!isView) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName("viora");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("viospice Command Line Interface");
    parser.addVersionOption();

    QCommandLineOption analysisOption(QStringList() << "a" << "analysis", "Analysis type (op, tran, ac)", "type", "op");
    parser.addOption(analysisOption);

    QCommandLineOption stopOption(QStringList() << "t" << "stop", "Stop time for transient", "time", "10m");
    parser.addOption(stopOption);

    QCommandLineOption stepOption(QStringList() << "s" << "step", "Step size for transient", "time", "100u");
    parser.addOption(stepOption);

    QCommandLineOption maxStepOption("max-step", "Maximum timestep for interactive/live mode (seconds)", "time", "1e-3");
    parser.addOption(maxStepOption);

    QCommandLineOption maxTimeOption("max-time", "Maximum simulation time for interactive/live mode (seconds, 0=unlimited)", "time", "0");
    parser.addOption(maxTimeOption);

    QCommandLineOption maxPtsOption("max-pts", "Maximum data points for interactive/live mode", "count", "100000");
    parser.addOption(maxPtsOption);

    QCommandLineOption jsonOption("json", "Output results in JSON format");
    QCommandLineOption transparentOption("transparent", "Render PNG with transparent background (schematic-render, symbol-render)");
    QCommandLineOption includeCommentsOption("include-comments", "Parse commented .model/.subckt lines (library-index)");
    QCommandLineOption schemaOption("schema", "Print JSON schema for the command and exit");
    QCommandLineOption scaleOption("scale", "Render scale (default 4.0)", "scale", "4");
    QCommandLineOption quietOption("quiet", "Silence non-JSON output");
    QCommandLineOption debugOption("debug", "Enable verbose debug output.");
    QCommandLineOption exitWarnOption("exit-on-warning", "Exit with non-zero code if warnings appear (netlist-run/netlist-validate)");
    QCommandLineOption nameOption("name", "Name of subcircuit or symbol", "name");
    QCommandLineOption symTypeOption("symbol-type", "Type of symbol to generate (ic, op)", "type", "ic");
    QCommandLineOption mappingOption("mapping", "Mapping JSON file for automatic symbol assignment", "mapping.json");
    QCommandLineOption helpOption(QStringList() << "h" << "help", "Show help for a command");
    QCommandLineOption noColorOption("no-color", "Disable colored output");
    QCommandLineOption renameNetOption("rename-net", "Rename net label (repeatable): old=new", "pair");
    QCommandLineOption normalizeValueOption("normalize-value", "Normalize value (repeatable): old=new", "pair");
    QCommandLineOption prefixRefOption("prefix-ref", "Rename reference prefix: old=new", "pair");
    QCommandLineOption probeListOption("list", "List available signals (schematic-probe)");
    QCommandLineOption probeAddOption("add", "Add probe (repeatable): V(net) or I(device)", "signal");
    QCommandLineOption probeAutoOption("auto", "Auto-probe all nets (schematic-probe)");
    QCommandLineOption symbolNameOption("symbol-name", "Symbol name (for KiCad .kicad_sym import)", "symname");
    QCommandLineOption timeoutOption("timeout", "Netlist run timeout (e.g. 10s, 5000ms)", "time", "10s");
    QCommandLineOption tValueOption("time", "Time value for FluxScript template execution", "value", "0.0");
    QCommandLineOption inputsOption("inputs", "Input values for FluxScript template (comma separated)", "values", "0.0");
    QCommandLineOption formatOption(QStringList() << "f" << "format", "Output format (netlist: spice|json, raw-export: csv|json|parquet)", "format", "spice");
    QCommandLineOption signalOption("signal", "Signal to export (repeatable, raw-export)", "signame");
    QCommandLineOption exportRawOption("export-raw", "Export raw data after netlist-run (csv|json)", "rawformat");
    QCommandLineOption maxPointsOption("max-points", "Limit exported samples (raw-export, netlist-run --export-raw)", "pointcount");
    QCommandLineOption baseSignalOption("base-signal", "Signal to drive decimation (raw-export, netlist-run --export-raw)", "basesig");
    QCommandLineOption statsOption("stats", "Export signal statistics after netlist-run (min/max/avg/rms)");
    QCommandLineOption rangeOption("range", "Limit exported samples to time window (t0:t1)", "trange");
    QCommandLineOption measureOption("measure", "Compute measurement (repeatable). Examples: V(net1)_max, I(V1)_rms, V(net1)@t=1ms", "mexpr");
    QCommandLineOption measureFormatOption("measure-format", "Measure output format (text|json)", "mformat", "text");
    QCommandLineOption assertOption("assert", "Fail if assertion is false (repeatable). Examples: \"V(OUT) > 4.5\", \"V(OUT)_min > 4.5\"", "aexpr");
    QCommandLineOption summaryOption("summary", "Show concise summary (raw-info)");
    QCommandLineOption recursiveOption("recursive", "Scan subdirectories recursively (library-to-symbols)");
    QCommandLineOption viewTypeOption("type", "Viewer type (plot, osc)", "viewtype", "plot");

    QCommandLineOption signalRegexOption("signal-regex", "Filter signals by regex (raw-export)", "pattern");
    QCommandLineOption moduleOption("module", "Module name to inspect (verilog-inspect)", "modname");
    parser.addOption(moduleOption);
    QCommandLineOption outOption(QStringList() << "out" << "output", "Write output to file (schematic-netlist)", "outfile");
    QCommandLineOption reportTitleOption("report-title", "Report title", "rtitle", "VioSpice Design Review");
    QCommandLineOption reportAuthorOption("report-author", "Report author", "rauthor", "VioSpice");
    QCommandLineOption noSchematicOption("no-schematic", "Exclude schematic section from report");
    QCommandLineOption noWaveformsOption("no-waveforms", "Exclude waveforms section from report");
    QCommandLineOption noMeasurementsOption("no-measurements", "Exclude measurements section from report");
    QCommandLineOption noNetlistOption("no-netlist", "Exclude netlist section from report");
    QCommandLineOption rawFileOption("raw-file", "Simulation results file (.raw) to include in report", "rawfile");
    QCommandLineOption schematicPngOption("schematic-png", "Schematic image file (.png) to embed in report", "pngfile");
    QCommandLineOption clipboardOption("clipboard", "Copy screenshot to clipboard");
    QCommandLineOption includeHiddenOption("include-hidden", "Include hidden/docked widgets");
    QCommandLineOption listChildrenOption("list-children", "List child widgets of a parent window");
    QCommandLineOption watchOption("watch", "Continuously capture frames");
    QCommandLineOption intervalOption("interval", "Interval for --watch mode in ms", "ms", "1000");
    QCommandLineOption outputDirOption("output-dir", "Directory for --watch mode frames", "dir");
    QCommandLineOption regionOption("region", "Capture a specific region x,y,w,h", "region");
    QCommandLineOption windowOption("window", "Target window name", "name", "SchematicEditor");
    QCommandLineOption shareTitleOption("share-title", "Share title", "stitle", "");
    QCommandLineOption shareDescOption("share-description", "Share description", "sdesc", "");
    QCommandLineOption shareUploadOption("upload", "Upload to server instead of URL (share)");
    QCommandLineOption shareCopyOption("copy", "Copy URL to clipboard after sharing", "copy");
    QCommandLineOption shareServerOption("server", "Share server URL", "url", "http://localhost:8765");
    QCommandLineOption robustOption("robust", "Enable robust simulation mode (adds damping and improves convergence)");
    QCommandLineOption compatOption("compat", "Apply LTspice compatibility layer to raw netlist before running (netlist-run)");
    parser.addOption(robustOption);
    parser.addOption(compatOption);
    parser.addOption(shareTitleOption);
    parser.addOption(shareDescOption);
    parser.addOption(shareUploadOption);
    parser.addOption(shareCopyOption);
    parser.addOption(shareServerOption);
    parser.addOption(jsonOption);
    parser.addOption(transparentOption);
    parser.addOption(includeCommentsOption);
    parser.addOption(schemaOption);
    parser.addOption(scaleOption);
    parser.addOption(quietOption);
    parser.addOption(debugOption);
    parser.addOption(exitWarnOption);
    parser.addOption(nameOption);
    parser.addOption(symTypeOption);
    parser.addOption(mappingOption);
    parser.addOption(helpOption);
    parser.addOption(noColorOption);
    parser.addOption(renameNetOption);
    parser.addOption(normalizeValueOption);
    parser.addOption(prefixRefOption);
    parser.addOption(probeListOption);
    parser.addOption(probeAddOption);
    parser.addOption(probeAutoOption);
    parser.addOption(symbolNameOption);
    parser.addOption(timeoutOption);
    parser.addOption(tValueOption);
    parser.addOption(inputsOption);
    parser.addOption(formatOption);
    parser.addOption(signalOption);
    parser.addOption(exportRawOption);
    parser.addOption(maxPointsOption);
    parser.addOption(baseSignalOption);
    parser.addOption(statsOption);
    parser.addOption(rangeOption);
    parser.addOption(measureOption);
    parser.addOption(measureFormatOption);
    parser.addOption(assertOption);
    parser.addOption(summaryOption);
    parser.addOption(recursiveOption);
    parser.addOption(viewTypeOption);
    parser.addOption(signalRegexOption);
    parser.addOption(outOption);
    parser.addOption(reportTitleOption);
    parser.addOption(reportAuthorOption);
    parser.addOption(noSchematicOption);
    parser.addOption(noWaveformsOption);
    parser.addOption(noMeasurementsOption);
    parser.addOption(noNetlistOption);
    parser.addOption(rawFileOption);
    parser.addOption(schematicPngOption);
    parser.addOption(clipboardOption);
    parser.addOption(includeHiddenOption);
    parser.addOption(listChildrenOption);
    parser.addOption(watchOption);
    parser.addOption(intervalOption);
    parser.addOption(outputDirOption);
    parser.addOption(regionOption);
    parser.addOption(windowOption);

    // Positional arguments
    parser.addPositionalArgument("command", "Command to run: drc, erc, simulate, netlist-run, netlist-validate, raw-info, raw-export, view, verilog-inspect, render, schematic-render, symbol-render, symbol-query, symbol-validate, symbol-list, symbol-export, symbol-import, library-index, schematic-query, schematic-netlist, schematic-bom, schematic-validate, schematic-diff, schematic-transform, schematic-probe, netlist-compare, generate-report, share, audit, autofix, process, python, plugins-smoke, plugin-pack, plugin-inspect, screenshot, gui");
    parser.addPositionalArgument("file", "File to process (.pcb or .sch), except for plugins-smoke");
    parser.addPositionalArgument("script", "JSON script file for 'process' command", "");

    parser.process(app);
    g_debug = parser.isSet("debug");
    g_quiet = parser.isSet("quiet");
    g_exitOnWarning = parser.isSet("exit-on-warning");
    g_noColor = parser.isSet("no-color");
    if (g_noColor) {
        qputenv("NO_COLOR", "1");
    }
    const bool jsonRequested = parser.isSet("json") || app.arguments().contains("--json");
    if (jsonRequested) {
        g_quiet = true;
    }
    if (g_quiet || jsonRequested) {
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n"));
    }

    const QStringList args = parser.positionalArguments();
    if (parser.isSet("help")) {
        if (args.size() > 0) {
            printCommandHelp(args.at(0));
        } else {
            printGeneralHelp();
        }
        return 0;
    }

    if (args.size() > 0) {
        QString command = args.at(0);
        if (command == "item-render") {
            SchematicItemRegistry::registerBuiltInItems();
            return runItemRender(args, parser) ? 0 : 1;
        }
        if (command == "screenshot") {
            return runScreenshot(QCoreApplication::arguments(), parser) ? 0 : 1;
        }
        if (command == "gui") {
            return runGui(QCoreApplication::arguments(), parser) ? 0 : 1;
        }
    }

    if (args.size() < 1) {
        printGeneralHelp();
        return 1;
    }

    QString command = args.at(0);
    if (command == "help") {
        if (args.size() > 1) {
            printCommandHelp(args.at(1));
        } else {
            printGeneralHelp();
        }
        return 0;
    }

    if (command == "plugin-pack") {
        return runPluginPack(args) ? 0 : 1;
    }

    if (command == "plugin-inspect") {
        return runPluginInspect(args) ? 0 : 1;
    }

    if (command == "extension") {
        if (args.size() < 2) {
            std::cerr << "Usage: viora extension <init|validate|install> [name|dir]\n";
            return 1;
        }
        QString action = args[1];
        QStringList rest = args.mid(2);
        if (action == "init")      return cmdExtensionInit(rest);
        if (action == "validate")  return cmdExtensionValidate(rest);
        if (action == "install")   return cmdExtensionInstall(rest);
        std::cerr << "Unknown extension action: " << action.toStdString() << "\n";
        return 1;
    }

    if (parser.isSet("schema")) {
        printSchema(command);
        return 0;
    }

    // FluxScript integration command
    if (command == "flux") {
        VioSpice::FluxCommand fluxCmd;
        QStringList fluxArgs;
        for (int i = 1; i < args.size(); ++i) {
            fluxArgs << args.at(i);
        }
        return fluxCmd.run(fluxArgs, parser, g_quiet);
    }

    if (command == "plugins-smoke") {
        PluginManager::instance().unloadPlugins();
        PluginManager::instance().loadPlugins();

        const auto plugins = PluginManager::instance().activePlugins();
        if (!g_quiet) std::cout << "Loaded plugins: " << plugins.size() << std::endl;
        for (FluxPlugin* plugin : plugins) {
            if (!g_quiet) std::cout << "  - " << plugin->name().toStdString()
                      << " v" << plugin->version().toStdString()
                      << " (sdk " << plugin->sdkVersionMajor()
                      << "." << plugin->sdkVersionMinor() << ")" << std::endl;
        }

        PluginManager::instance().unloadPlugins();
        if (!g_quiet) std::cout << "Plugin lifecycle smoke test completed." << std::endl;
        return 0;
    }

    if (args.size() < 2) {
        parser.showHelp();
        return 1;
    }

    QString filePath = args.at(1);

    if (command != "library-to-symbols" && !QFileInfo::exists(filePath)) {
        std::cerr << "Error: File not found: " << filePath.toStdString() << std::endl;
        return 1;
    }

    // Register items for correct deserialization
    #if VIOSPICE_HAS_PCB
    PCBItemRegistry::registerBuiltInItems();
    #endif
    SchematicItemRegistry::registerBuiltInItems();
    
    // Initialize libraries synchronously for CLI
    SymbolLibraryManager::instance().loadUserLibraries(QDir::homePath() + "/ViospiceLib/sym");
    ModelLibraryManager::instance().reload();

    if (command == "drc") {
        #if !VIOSPICE_HAS_PCB
        std::cerr << "PCB features are not available in this build." << std::endl;
        return 1;
        #else
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (!g_quiet) std::cout << "Running DRC on " << filePath.toStdString() << "..." << std::endl;
        PCBDRC drc;
        drc.runFullCheck(&scene);

        if (drc.violations().isEmpty()) {
            if (!g_quiet) std::cout << "DRC Passed! No violations found." << std::endl;
        } else {
            if (!g_quiet) std::cout << "DRC Failed! Found " << drc.violations().size() << " violations:" << std::endl;
            for (const auto& v : drc.violations()) {
                if (!g_quiet) std::cout << "  [" << v.severityString().toStdString() << "] " 
                          << v.typeString().toStdString() << ": "
                          << v.message().toStdString() << " at (" 
                          << v.location().x() << ", " << v.location().y() << ")" << std::endl;
            }
            return drc.errorCount() > 0 ? 1 : 0;
        }
        #endif
    } else if (command == "erc") {
        QGraphicsScene scene;
        QString pageSize;
        TitleBlockData dummyTB;
        if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
            std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (!g_quiet) std::cout << "Running ERC on " << filePath.toStdString() << "..." << std::endl;
        auto violations = SchematicERC::run(&scene, QFileInfo(filePath).absolutePath());

        if (violations.isEmpty()) {
            if (!g_quiet) std::cout << "ERC Passed! No issues found." << std::endl;
        } else {
            if (!g_quiet) std::cout << "ERC found " << violations.size() << " issues:" << std::endl;
            for (const auto& v : violations) {
                QString sev = (v.severity == ERCViolation::Error) ? "Error" : "Warning";
                if (!g_quiet) std::cout << "  [" << sev.toStdString() << "] " 
                          << v.message.toStdString() << " at (" 
                          << v.position.x() << ", " << v.position.y() << ")" << std::endl;
            }
        }
    } else if (command == "simulate") {
        QGraphicsScene scene;
        QString pageSize;
        TitleBlockData dummyTB;
        if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
            std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (!g_quiet) std::cerr << "Simulating circuit " << filePath.toStdString() << " (Ngspice backend)..." << std::endl;
        
        QString analysisType = parser.value("analysis").toLower();
        SpiceNetlistGenerator::SimulationParams spiceParams;
        SimAnalysisType t = SimAnalysisType::OP;

        if (analysisType == "live") {
            t = SimAnalysisType::RealTime;

            if (!g_quiet) std::cerr << "  - Type: Interactive Live (MaxStep=" << parser.value("max-step").toStdString()
                      << "s, MaxTime=" << parser.value("max-time").toStdString()
                      << "s, MaxPts=" << parser.value("max-pts").toStdString() << ")" << std::endl;

            double maxStep = 1e-3;
            SimValueParser::parseSpiceNumber(parser.value("max-step"), maxStep);
            double maxTime = 0.0;
            SimValueParser::parseSpiceNumber(parser.value("max-time"), maxTime);
            int maxPts = parser.value("max-pts").toInt();
            if (maxStep <= 0) maxStep = 1e-3;
            if (maxPts < 1000) maxPts = 100000;

            // Use SimManager for the interactive live stream
            auto &sm = SimManager::instance();
            bool finished = false;
            QString lastError;
            SimResults collected;

            // Wire up signals
            QObject::connect(&sm, &SimManager::simulationStopped, [&]() { finished = true; });
            QObject::connect(&sm, &SimManager::errorOccurred, [&](const QString &err) {
                lastError = err;
                finished = true;
            });
            QObject::connect(&sm, &SimManager::realTimeDataBatchReceived, [&](const std::vector<double> &,
                                                                               const std::vector<std::vector<double>> &,
                                                                               const QStringList &) {
                // Data is streaming; we just track that it's alive
            });

            sm.runRealTime(&scene, nullptr, maxStep, maxTime, maxPts);

            QElapsedTimer wallClock;
            wallClock.start();
            const qint64 maxWallMs = (maxTime > 0) ? static_cast<qint64>(maxTime * 1000 * 1.5 + 60000) : 24LL * 3600 * 1000;

            while (!finished && wallClock.elapsed() < maxWallMs) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
                QThread::msleep(10);
                if (!sm.isRunning() && !finished) {
                    QCoreApplication::processEvents();
                    finished = true;
                }
            }

            sm.stopRealTime();
            if (!lastError.isEmpty()) {
                std::cerr << "Interactive simulation failed: " << lastError.toStdString() << std::endl;
                return 1;
            }
            if (!g_quiet) std::cerr << "\nInteractive simulation completed." << std::endl;
            std::_Exit(0);

        } else if (analysisType == "tran") {
            t = SimAnalysisType::Transient;
            spiceParams.type = SpiceNetlistGenerator::Transient;
            spiceParams.stop = parser.value("stop");
            spiceParams.step = parser.value("step");
            if (spiceParams.stop.isEmpty()) spiceParams.stop = "10m";
            if (spiceParams.step.isEmpty()) spiceParams.step = "100u";
            if (!g_quiet) std::cerr << "  - Type: Transient (Stop=" << spiceParams.stop.toStdString() << ", Step=" << spiceParams.step.toStdString() << ")" << std::endl;
        } else if (analysisType == "ac") {
            t = SimAnalysisType::AC;
            spiceParams.type = SpiceNetlistGenerator::AC;
            spiceParams.start = "10";
            spiceParams.stop = "1meg";
            spiceParams.step = "100";
            if (!g_quiet) std::cerr << "  - Type: AC Sweep (10Hz to 1MHz)" << std::endl;
        } else {
            t = SimAnalysisType::OP;
            spiceParams.type = SpiceNetlistGenerator::OP;
            if (!g_quiet) std::cerr << "  - Type: DC Operating Point" << std::endl;
        }

        auto result = SpiceNetlistGenerator::generate(&scene, QFileInfo(filePath).absolutePath(), nullptr, spiceParams);
        
        QTemporaryFile tempNetlist(QDir::tempPath() + "/viospice_cli_XXXXXX.cir");
        tempNetlist.setAutoRemove(false);
        if (!tempNetlist.open()) {
            std::cerr << "Error creating temporary netlist file." << std::endl;
            return 1;
        }
        {
            QTextStream out(&tempNetlist);
            out << result.netlist;
        }
        tempNetlist.close();

        QEventLoop loop;
        SimResults results;
        bool success = false;
        QString lastError;

        auto& sm = SimulationManager::instance();
        QObject::connect(&sm, &SimulationManager::simulationFinished, &loop, &QEventLoop::quit);
        QObject::connect(&sm, &SimulationManager::errorOccurred, [&](const QString& err) {
            lastError = err;
            loop.quit();
        });
        
        QObject::connect(&sm, &SimulationManager::rawResultsReady, [&](const QString& path) {
            RawData rd;
            if (RawDataParser::loadRawAscii(path.toStdString(), &rd)) {
                // Simplified SimResults conversion for CLI
                results.analysisType = t;
                double maxVoltage = 0.0;
                for (int i = 0; i < (int)rd.varNames.size(); ++i) {
                    if (i == 0) continue; // skip time/frequency
                    SimWaveform wave;
                    wave.name = rd.varNames[i];
                    wave.xData = std::vector<double>(rd.x.begin(), rd.x.end());
                    wave.yData = std::vector<double>(rd.y[i-1].begin(), rd.y[i-1].end());
                    results.waveforms.push_back(wave);
                    
                    // Populate summaries
                    if (!rd.y[i-1].empty()) {
                        double lastVal = rd.y[i-1].back();
                        maxVoltage = std::max(maxVoltage, std::abs(lastVal));
                        QString qName = QString::fromStdString(wave.name);
                        if (qName.startsWith("V(", Qt::CaseInsensitive)) {
                             results.nodeVoltages[qName.mid(2, qName.size() - 3).toStdString()] = lastVal;
                        } else if (qName.startsWith("I(", Qt::CaseInsensitive)) {
                             results.branchCurrents[qName.mid(2, qName.size() - 3).toStdString()] = lastVal;
                        }
                    }
                }
                if (maxVoltage > 10000.0 && !g_quiet) {
                    std::cerr << "Warning: Extreme voltage detected (>10kV). Circuit may be unstable or unphysical." << std::endl;
                }
                success = true;
            }
        });

        sm.runSimulation(tempNetlist.fileName(), nullptr);
        
        // Active wait loop with safety timeout
        QElapsedTimer activeTimer;
        activeTimer.start();
        while (!success && !lastError.length() && activeTimer.elapsed() < 30000) {
            QCoreApplication::processEvents();
            SimulationManager::instance().applyPendingFluxSourceUpdates();
            QThread::msleep(5);
            if (!sm.isRunning() && !success) {
                // Give it one more loop to process the final event
                QCoreApplication::processEvents();
                if (!success) break;
            }
        }

        sm.shutdown(); // Ensure backend is fully stopped before cleanup
        QThread::msleep(50); // Final cooldown to avoid race with RawDataParser
        
        QFile::remove(tempNetlist.fileName());
        QFile::remove(tempNetlist.fileName() + ".raw");

        if (!success) {
            std::cerr << "Simulation failed: " << lastError.toStdString() << std::endl;
            SimulationManager::instance().shutdown();
            return 1;
        }

        if (parser.isSet("json")) {
            printJsonValue(resultsToJson(results));
            std::cout.flush();
            std::cerr.flush();
            std::_Exit(0);
        }

        if (results.waveforms.empty() && results.nodeVoltages.empty()) {
            std::cerr << "Simulation failed to produce results." << std::endl;
            SimulationManager::instance().shutdown();
            return 1;
        }

        if (t == SimAnalysisType::OP) {
            if (!g_quiet) std::cerr << "\n--- DC Operating Point Results ---" << std::endl;
            for (const auto& [node, v] : results.nodeVoltages) {
                if (!g_quiet) std::cerr << "V(" << node << ") = " << v << " V" << std::endl;
            }
            for (const auto& [branch, i] : results.branchCurrents) {
                if (!g_quiet) std::cerr << "I(" << branch << ") = " << (i * 1000.0) << " mA" << std::endl;
            }
        } else {
            if (!g_quiet) std::cerr << "\nGenerated " << results.waveforms.size() << " waveforms." << std::endl;
            for (const auto& wave : results.waveforms) {
                if (!g_quiet) std::cerr << "  - " << wave.name << " (" << wave.yData.size() << " points)" << std::endl;
                if (!wave.yData.empty()) {
                    if (!g_quiet) std::cerr << "    Range: [" << *std::min_element(wave.yData.begin(), wave.yData.end()) 
                              << " V, " << *std::max_element(wave.yData.begin(), wave.yData.end()) << " V]" << std::endl;
                }
            }
        }

        if (!g_quiet) std::cerr << "\nSimulation successful." << std::endl;
        std::cout.flush();
        std::cerr.flush();
        std::_Exit(0);
    } else if (command == "render") {
        #if !VIOSPICE_HAS_PCB
        std::cerr << "PCB features are not available in this build." << std::endl;
        return 1;
        #else
        if (args.size() < 3) {
            std::cerr << "Usage: viora render <file.pcb> <output.png>" << std::endl;
            return 1;
        }
        QString output = args.at(2);
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (!g_quiet) std::cout << "Rendering " << filePath.toStdString() << " to " << output.toStdString() << "..." << std::endl;
        
        QRectF rect = scene.itemsBoundingRect();
        if (rect.isEmpty()) rect = QRectF(-50, -50, 100, 100);
        rect.adjust(-10, -10, 10, 10); // Adding margin

        QImage image(rect.size().toSize() * 4, QImage::Format_ARGB32); // 4x scale for high res
        image.fill(QColor(20, 20, 25)); // Dark board color
        
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        scene.render(&painter, QRectF(), rect);
        painter.end();

        if (image.save(output)) {
            if (!g_quiet) std::cout << "Successfully rendered scene to " << output.toStdString() << std::endl;
        } else {
            std::cerr << "Failed to save image to " << output.toStdString() << std::endl;
            return 1;
        }
        #endif
    } else if (command == "schematic-render") {
        if (args.size() < 3) {
            std::cerr << "Usage: viora schematic-render <file.flxsch> <out.png>" << std::endl;
            return 1;
        }
        return runSchematicRender(filePath, args.at(2), parser) ? 0 : 1;
    } else if (command == "generate-report") {
        if (args.size() < 3) {
            std::cerr << "Usage: viora generate-report <file.flxsch> <out.html> [--title 'My Design'] [--author 'John Doe']" << std::endl;
            return 1;
        }
        return runGenerateReport(filePath, args.at(2), parser) ? 0 : 1;
    } else if (command == "share") {
        if (args.size() < 2) {
            std::cerr << "Usage: viora share <file.flxsch> [--title 'My Circuit'] [--description 'Description'] [--upload] [--copy] [--server <url>]" << std::endl;
            return 1;
        }
        return runShareSchematic(args.at(1), parser) ? 0 : 1;
    } else if (command == "symbol-render") {
        return runSymbolRender(args, parser) ? 0 : 1;
    } else if (command == "symbol-query") {
        return runSymbolQuery(args) ? 0 : 1;
    } else if (command == "symbol-validate") {
        return runSymbolValidate(args) ? 0 : 1;
    } else if (command == "symbol-search") {
        return runSymbolSearch(args) ? 0 : 1;
    } else if (command == "symbol-list") {
        return runSymbolList(args) ? 0 : 1;
    } else if (command == "symbol-export") {
        return runSymbolExport(args) ? 0 : 1;
    } else if (command == "symbol-import") {
        return runSymbolImport(args, parser) ? 0 : 1;
    } else if (command == "symbol-from-subckt") {
        return runSymbolFromSubckt(args, parser) ? 0 : 1;
    } else if (command == "library-to-symbols") {
        return runLibraryToSymbols(args, parser) ? 0 : 1;
    } else if (command == "library-auto-convert") {
        return runLibraryAutoConvert(args, parser) ? 0 : 1;
    } else if (command == "library-index") {
        return runLibraryIndex(args, parser) ? 0 : 1;
    } else if (command == "schematic-query") {
        return runSchematicQuery(filePath) ? 0 : 1;
    } else if (command == "schematic-bom") {
        return runSchematicBom(filePath) ? 0 : 1;
    } else if (command == "schematic-validate") {
        return runSchematicValidate(filePath) ? 0 : 1;
    } else if (command == "schematic-diff") {
        return runSchematicDiff(args) ? 0 : 1;
    } else if (command == "schematic-transform") {
        return runSchematicTransform(filePath, parser) ? 0 : 1;
    } else if (command == "schematic-probe") {
        return runSchematicProbe(filePath, parser) ? 0 : 1;
    } else if (command == "netlist-compare") {
        return runNetlistCompare(args, parser) ? 0 : 1;
    } else if (command == "netlist-run") {
        return runNetlistRun(filePath, parser) ? 0 : 1;
    } else if (command == "netlist-validate") {
        return runNetlistValidate(filePath, parser) ? 0 : 1;
    } else if (command == "netlist-to-schematic") {
        return runNetlistToSchematic(filePath, parser) ? 0 : 1;
    } else if (command == "raw-info") {
        return runRawInfo(filePath, parser) ? 0 : 1;
    } else if (command == "raw-export") {
        return runRawExport(filePath, parser) ? 0 : 1;
    } else if (command == "raw-stats") {
        return runRawStats(filePath, parser) ? 0 : 1;
    } else if (command == "view") {
        QString viewType = parser.value("type").toLower();
        if (viewType == "osc" || viewType == "oscilloscope") {
            if (runViewOsc(filePath, parser)) {
                return app.exec();
            }
        } else {
            if (runViewRaw(filePath, parser)) {
                return app.exec();
            }
        }
        return 1;
    } else if (command == "verilog-inspect") {
        return runVerilogInspect(filePath, parser) ? 0 : 1;
    } else if (command == "schematic-netlist") {
        return runSchematicNetlist(filePath, parser) ? 0 : 1;
    } else if (command == "audit") {
        if (!g_quiet) std::cout << "Project Doctor (Audit) starting on: " << filePath.toStdString() << "..." << std::endl;
        
        QJsonObject report;
        report["project_path"] = filePath;
        report["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        
        QJsonArray pcbAudits;
        QJsonArray schAudits;

        QFileInfo info(filePath);
        QStringList pcbFiles;
        QStringList schFiles;

        if (info.isDir()) {
            QDir dir(filePath);
            for (const QString& f : dir.entryList({"*.pcb"}, QDir::Files)) pcbFiles << dir.filePath(f);
            for (const QString& f : dir.entryList({"*.sch"}, QDir::Files)) schFiles << dir.filePath(f);
        } else {
            if (filePath.endsWith(".pcb")) pcbFiles << filePath;
            else if (filePath.endsWith(".sch")) schFiles << filePath;
        }

        // Run DRC on PCBs
        #if VIOSPICE_HAS_PCB
        for (const QString& pcbFile : pcbFiles) {
            QJsonObject pcbReport;
            pcbReport["file"] = QFileInfo(pcbFile).fileName();
            
            QGraphicsScene scene;
            if (PCBFileIO::loadPCB(&scene, pcbFile)) {
                PCBDRC drc;
                drc.runFullCheck(&scene);
                
                QJsonArray violations;
                for (const auto& v : drc.violations()) {
                    QJsonObject vio;
                    vio["type"] = v.typeString();
                    vio["severity"] = v.severityString();
                    vio["message"] = v.message();
                    vio["x"] = v.location().x();
                    vio["y"] = v.location().y();
                    violations.append(vio);
                }
                pcbReport["violations"] = violations;
                pcbReport["status"] = drc.errorCount() == 0 ? "Healthy" : "Needs Attention";
            } else {
                pcbReport["status"] = "Error Loading";
            }
            pcbAudits.append(pcbReport);
        }
        #endif

        // Run ERC on Schematics
        for (const QString& schFile : schFiles) {
            QJsonObject schReport;
            schReport["file"] = QFileInfo(schFile).fileName();
            
            QGraphicsScene scene;
            QString pageSize;
            TitleBlockData dummyTB;
            if (SchematicFileIO::loadSchematic(&scene, schFile, pageSize, dummyTB)) {
                auto violations = SchematicERC::run(&scene, QFileInfo(schFile).absolutePath());
                
                QJsonArray issues;
                for (const auto& v : violations) {
                    QJsonObject issue;
                    issue["severity"] = (v.severity == ERCViolation::Error) ? "Error" : 
                                       (v.severity == ERCViolation::Critical) ? "Critical" : "Warning";
                    issue["message"] = v.message;
                    issue["x"] = v.position.x();
                    issue["y"] = v.position.y();
                    issues.append(issue);
                }
                schReport["issues"] = issues;
                schReport["status"] = issues.isEmpty() ? "Healthy" : "Needs Attention";
            } else {
                schReport["status"] = "Error Loading";
            }
            schAudits.append(schReport);
        }

        report["pcb_reports"] = pcbAudits;
        report["schematic_reports"] = schAudits;
        report["overall_status"] = (pcbFiles.size() + schFiles.size() > 0) ? "Audit Complete" : "No files found";

        QJsonDocument doc(sortJsonValue(report).toObject());
        QDir().mkpath("docs/reports");
        QString reportPath = "docs/reports/project_health_report.json";
        QFile file(reportPath);
        if (file.open(QFile::WriteOnly)) {
            file.write(doc.toJson());
            file.close();
            if (!g_quiet) std::cout << "Project health report generated: " << reportPath.toStdString() << std::endl;
        } else {
            std::cerr << "Failed to save health report." << std::endl;
        }

    } else if (command == "autofix") {
        if (!g_quiet) std::cout << "Project Autofix starting on: " << filePath.toStdString() << "..." << std::endl;
        
        if (filePath.endsWith(".sch")) {
            QGraphicsScene scene;
            QString pageSize;
            TitleBlockData dummyTB;
            if (SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
                int fixedCount = 0;
                
                // 1. Run Annotation
                SchematicAnnotator::annotate(&scene, false); // Only fix '?'
                if (!g_quiet) std::cout << "  - Reference annotation completed." << std::endl;

                // 2. Remove duplicate/redundant wires
                QList<WireItem*> wires;
                for (auto* item : scene.items()) {
                    if (auto* w = dynamic_cast<WireItem*>(item)) wires.append(w);
                }

                for (int i = 0; i < wires.size(); ++i) {
                    for (int j = i + 1; j < wires.size(); ++j) {
                        if (wires[i]->startPoint() == wires[j]->startPoint() && 
                            wires[i]->endPoint() == wires[j]->endPoint()) {
                            scene.removeItem(wires[j]);
                            delete wires[j];
                            wires.removeAt(j);
                            j--;
                            fixedCount++;
                        }
                    }
                }
                
                if (!g_quiet && fixedCount > 0) std::cout << "  - Removed " << fixedCount << " duplicate wires." << std::endl;
                
                if (SchematicFileIO::saveSchematic(&scene, filePath, pageSize)) {
                    if (!g_quiet) std::cout << "  - Schematic fixed and saved successfully." << std::endl;
                }
            }
        } else if (filePath.endsWith(".pcb")) {
            #if !VIOSPICE_HAS_PCB
            std::cerr << "PCB features are not available in this build." << std::endl;
            return 1;
            #else
            QGraphicsScene scene;
            if (PCBFileIO::loadPCB(&scene, filePath)) {
                int fixedTraces = 0;
                int snappedPoints = 0;
                int snappedComponents = 0;
                PCBDRC drc;
                double minWidth = drc.rules().minTraceWidth();
                double gridSize = 0.1; // 0.1mm grid for snapping

                // Helper to snap a point to grid
                auto snap = [&](QPointF p) {
                    double x = std::round(p.x() / gridSize) * gridSize;
                    double y = std::round(p.y() / gridSize) * gridSize;
                    return QPointF(x, y);
                };

                // 1. Grid Snapping (Shared points)
                QMap<QString, QPointF> pointMap; 
                auto ptKey = [](QPointF p) { return QString("%1,%2").arg(p.x(), 0, 'f', 4).arg(p.y(), 0, 'f', 4); };

                QList<TraceItem*> traces;
                for (auto* item : scene.items()) {
                    if (auto* trace = dynamic_cast<TraceItem*>(item)) {
                        traces.append(trace);
                        if (trace->width() < minWidth) {
                            trace->setWidth(minWidth);
                            fixedTraces++;
                        }
                        pointMap[ptKey(trace->startPoint())] = snap(trace->startPoint());
                        pointMap[ptKey(trace->endPoint())] = snap(trace->endPoint());
                    } else if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
                        QPointF oldPos = comp->pos();
                        QPointF newPos = snap(oldPos);
                        if (oldPos != newPos) {
                            comp->setPos(newPos);
                            snappedComponents++;
                        }
                    }
                }

                // Apply trace grid snaps
                for (auto* trace : traces) {
                    QPointF oldS = trace->startPoint();
                    QPointF oldE = trace->endPoint();
                    QPointF newS = pointMap[ptKey(oldS)];
                    QPointF newE = pointMap[ptKey(oldE)];

                    if (newS != oldS || newE != oldE) {
                        trace->setStartPoint(newS);
                        trace->setEndPoint(newE);
                        snappedPoints++;
                    }
                }

                if (!g_quiet && fixedTraces > 0) std::cout << "  - Adjusted " << fixedTraces << " traces to minimum width (" << minWidth << "mm)." << std::endl;
                if (!g_quiet && snappedPoints > 0) std::cout << "  - Snapped " << snappedPoints << " trace points to " << gridSize << "mm grid." << std::endl;
                if (!g_quiet && snappedComponents > 0) std::cout << "  - Realigned " << snappedComponents << " components to " << gridSize << "mm grid." << std::endl;
                
                if (PCBFileIO::savePCB(&scene, filePath)) {
                    if (!g_quiet) std::cout << "  - PCB fixed and saved successfully." << std::endl;
                }
            }
            #endif
        } else {
            std::cerr << "Error: Autofix only supports .sch and .pcb files." << std::endl;
            return 1;
        }

    } else if (command == "process") {
        if (args.size() < 3) {
            std::cerr << "Usage: viora process <file.sch|.pcb> <script.json> [output.file]" << std::endl;
            return 1;
        }
        
        QString scriptPath = args.at(2);
        QString outputPath = (args.size() >= 4) ? args.at(3) : filePath;
        
        QGraphicsScene scene;
        QFile scriptFile(scriptPath);
        if (!scriptFile.open(QIODevice::ReadOnly)) {
            std::cerr << "Error reading script: " << scriptPath.toStdString() << std::endl;
            return 1;
        }
        
        QJsonDocument scriptDoc = QJsonDocument::fromJson(scriptFile.readAll());
        if (!scriptDoc.isArray()) {
            std::cerr << "Error: Script must be a JSON array of commands." << std::endl;
            return 1;
        }

        if (filePath.endsWith(".sch")) {
            SchematicAPI api(&scene);
            if (!api.load(filePath)) {
                std::cerr << "Error loading schematic: " << filePath.toStdString() << std::endl;
                return 1;
            }
            int count = api.executeBatch(scriptDoc.array());
            if (!g_quiet) std::cout << "Executed " << count << " schematic commands." << std::endl;
            if (api.save(outputPath)) {
                if (!g_quiet) std::cout << "Saved processed schematic to: " << outputPath.toStdString() << std::endl;
            } else {
                std::cerr << "Error saving processed schematic." << std::endl;
                return 1;
            }
        } else if (filePath.endsWith(".pcb")) {
            #if !VIOSPICE_HAS_PCB
            std::cerr << "PCB features are not available in this build." << std::endl;
            return 1;
            #else
            PCBAPI api(&scene);
            if (!api.load(filePath)) {
                std::cerr << "Error loading PCB: " << filePath.toStdString() << std::endl;
                return 1;
            }
            int count = api.executeBatch(scriptDoc.array());
            if (!g_quiet) std::cout << "Executed " << count << " PCB commands." << std::endl;
            if (api.save(outputPath)) {
                if (!g_quiet) std::cout << "Saved processed PCB to: " << outputPath.toStdString() << std::endl;
            } else {
                std::cerr << "Error saving processed PCB." << std::endl;
                return 1;
            }
            #endif
        } else {
            std::cerr << "Error: Unsupported file extension for process." << std::endl;
            return 1;
        }
    } else if (command == "python") {
        if (args.size() < 2) {
            std::cerr << "Usage: viora python <script.py> [args...]" << std::endl;
            return 1;
        }

        std::cerr << "Error: embedded Python support is not available in this build." << std::endl;
        return 1;
    } else {
        std::cerr << "Unknown command: " << command.toStdString() << std::endl;
        return 1;
    }
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(0);
}
