/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spice_netlist_generator.h"
#include "passes/component_extractor.h"
#include "passes/xspice_block_translator.h"
#include "passes/connectivity_evaluator.h"
#include "passes/model_injector.h"
#include "passes/netlist_formatter.h"
#include "passes/ltspice_rewriter.h"
#include "../items/schematic_item.h"
#include "../items/smart_signal_item.h"
#include "../items/virtual_terminal_item.h"
#include "net_manager.h"
#include "../io/netlist_generator.h"
#include "../../symbols/symbol_library.h"
#include "../../symbols/models/symbol_definition.h"
#include "../../simulator/bridge/model_library_manager.h"
#include "../items/schematic_spice_directive_item.h"
#include "../items/tuning_slider_symbol_item.h"
#include <QGraphicsScene>
#include <QSet>
#include <QMap>
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <cmath>
#include <limits>
#include <QJsonDocument>
#include <QJsonObject>
#include "config_manager.h"
#include "simulation_manager.h"
#include "../../simulator/core/sim_value_parser.h"
#include "../../simulator/mixedmode/NetlistManager.h"

using Flux::Model::SymbolDefinition;

namespace {
struct UserSpiceContentSummary {
    QSet<QString> declaredModelFiles;
    QSet<QString> declaredModelNames;
    QSet<QString> declaredElementRefs;
    QSet<QString> drivenRailNets;
    QStringList warnings;
    bool hasExplicitAnalysisCard = false;
    bool hasElementCards = false;
    bool hasLtspiceStartup = false;
    bool hasExplicitSaveDirective = false;
    bool hasNetDirective = false;
    bool isSParameter = false;
};

struct PassiveCompanionParams {
    QString baseValue;
    QString rser;
    QString rpar;
    QString cpar;
    QString ic;
};

bool isLikelyLogicInputPinName(const QString& rawPinName) {
    QString pin = rawPinName.trimmed().toLower();
    static const QRegularExpression re("(\\[[0-9]+\\]|<[0-9]+>|[0-9]+)$");
    pin.remove(re);
    if (pin.isEmpty()) return false;
    if (pin.contains("out") || pin == "q" || pin == "qn" || pin == "qbar" || pin == "y" || pin == "z" || pin == "f")
        return false;
    return pin.contains("in") || pin == "a" || pin == "b" || pin == "c" || pin == "d" ||
           pin == "e" || pin == "clk" || pin == "clock" || pin == "en" || pin == "enable" ||
           pin == "rst" || pin == "reset" || pin == "set" || pin == "s" || pin == "r" ||
           pin == "j" || pin == "k" || pin == "t";
}

PassiveCompanionParams parsePassiveCompanionParams(const QString& rawValue) {
    PassiveCompanionParams out;
    const QString text = rawValue.trimmed();
    if (text.isEmpty()) return out;

    static const QRegularExpression whitespaceRe("\\s+");
    const QStringList tokens = text.split(whitespaceRe, Qt::SkipEmptyParts);
    if (!tokens.isEmpty()) out.baseValue = tokens.first().trimmed();

    static const QRegularExpression rserRe("\\bRser\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rparRe("\\bRpar\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cparRe("\\bCpar\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression icRe("\\bic\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);

    if (auto match = rserRe.match(text); match.hasMatch()) out.rser = match.captured(1).trimmed();
    if (auto match = rparRe.match(text); match.hasMatch()) out.rpar = match.captured(1).trimmed();
    if (auto match = cparRe.match(text); match.hasMatch()) out.cpar = match.captured(1).trimmed();
    if (auto match = icRe.match(text); match.hasMatch()) out.ic = match.captured(1).trimmed();

    return out;
}

} // namespace







namespace {

struct VectorPinInfo {
    QString groupName;
    int index = -1;
    bool valid = false;
};

VectorPinInfo vectorPinInfo(const SymbolDefinition* sym, const QString& pinIdentifier, const QString& fallbackName) {
    if (sym) {
        if (const auto* pin = sym->pinPrimitive(pinIdentifier)) {
            const QString metaGroup = pin->data.value("signalVectorGroup").toString().trimmed();
            bool okIndex = false;
            const int metaIndex = pin->data.value("signalVectorIndex").toString().toInt(&okIndex);
            if (!metaGroup.isEmpty() && okIndex) {
                return {metaGroup, metaIndex, true};
            }
        }
    }

    const QString pinName = fallbackName.trimmed();
    static const QRegularExpression patterns[] = {
        QRegularExpression("^(.+)\\[(\\d+)\\]$"),
        QRegularExpression("^(.+)<(\\d+)>$"),
        QRegularExpression("^([A-Za-z_]+)(\\d+)$")
    };

    for (const QRegularExpression& re : patterns) {
        const QRegularExpressionMatch match = re.match(pinName);
        if (!match.hasMatch()) continue;

        bool okIndex = false;
        const int index = match.captured(2).toInt(&okIndex);
        if (!okIndex) continue;

        QString group = match.captured(1).trimmed();
        if (group.isEmpty()) continue;
        return {group, index, true};
    }

    return {};
}

struct XspicePinAssignment {
    QString pinIdentifier;
    QString pinName;
    QString netName;
    bool isInput = true;
    int order = 0;
    VectorPinInfo vector;
};

QStringList buildXspiceNodeTokens(const QList<XspicePinAssignment>& assignments,
                                  bool collapseScalarInputsToVector = false) {
    struct GroupedVector {
        bool isInput = true;
        int firstOrder = 0;
        QList<XspicePinAssignment> members;
    };

    QMap<QString, GroupedVector> groupedVectors;
    QMap<QString, int> groupOrder;
    QList<XspicePinAssignment> scalars;
    int nextGroupOrder = 0;

    for (const XspicePinAssignment& assignment : assignments) {
        if (assignment.vector.valid) {
            const QString key = QString("%1|%2").arg(assignment.isInput ? "I" : "O", assignment.vector.groupName);
            if (!groupedVectors.contains(key)) {
                GroupedVector group;
                group.isInput = assignment.isInput;
                group.firstOrder = assignment.order;
                groupedVectors.insert(key, group);
                groupOrder.insert(key, nextGroupOrder++);
            }
            groupedVectors[key].members.append(assignment);
            continue;
        }
        scalars.append(assignment);
    }

    std::sort(scalars.begin(), scalars.end(), [](const XspicePinAssignment& a, const XspicePinAssignment& b) {
        return a.order < b.order;
    });

    QList<QPair<int, QString>> orderedGroups;
    for (auto it = groupedVectors.constBegin(); it != groupedVectors.constEnd(); ++it) {
        orderedGroups.append(qMakePair(it.value().firstOrder, it.key()));
    }
    std::sort(orderedGroups.begin(), orderedGroups.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    QStringList inputs;
    QStringList outputs;

    auto appendScalar = [&](const XspicePinAssignment& assignment) {
        (assignment.isInput ? inputs : outputs).append(assignment.netName);
    };

    auto appendVector = [&](const GroupedVector& group) {
        QList<XspicePinAssignment> members = group.members;
        std::sort(members.begin(), members.end(), [](const XspicePinAssignment& a, const XspicePinAssignment& b) {
            if (a.vector.index != b.vector.index) return a.vector.index < b.vector.index;
            return a.order < b.order;
        });

        QStringList nets;
        for (const XspicePinAssignment& member : members) {
            nets.append(member.netName);
        }

        if (nets.size() == 1) {
            if (group.isInput) inputs.append(nets.first());
            else outputs.append(nets.first());
            return;
        }

        const QString token = "[" + nets.join(" ") + "]";
        if (group.isInput) inputs.append(token);
        else outputs.append(token);
    };

    int scalarIndex = 0;
    for (const auto& ordered : orderedGroups) {
        while (scalarIndex < scalars.size() && scalars[scalarIndex].order < ordered.first) {
            appendScalar(scalars[scalarIndex++]);
        }
        appendVector(groupedVectors.value(ordered.second));
    }
    while (scalarIndex < scalars.size()) {
        appendScalar(scalars[scalarIndex++]);
    }

    if (collapseScalarInputsToVector && inputs.size() > 1) {
        QStringList tokens;
        tokens.append("[" + inputs.join(" ") + "]");
        tokens.append(outputs);
        return tokens;
    }

    return inputs + outputs;
}

} // namespace









namespace {

QString defaultXspiceModelLine(const QString& ref, const QString& codeModel) {
    const QString modelName = QString("__XSPICE_%1").arg(XSpiceBlockTranslator::sanitizeMixedModeToken(ref));

    if (codeModel == "d_and" || codeModel == "d_nand" || codeModel == "d_or" ||
        codeModel == "d_nor" || codeModel == "d_xor" || codeModel == "d_xnor" ||
        codeModel == "d_buffer" || codeModel == "d_inverter" || codeModel == "d_tristate") {
        return QString(".model %1 %2(rise_delay=1n fall_delay=1n input_load=1p)")
            .arg(modelName, codeModel);
    }

    if (codeModel == "d_dff" || codeModel == "d_jkff" || codeModel == "d_tff" ||
        codeModel == "d_srff" || codeModel == "d_dlatch" || codeModel == "d_srlatch") {
        return QString(".model %1 %2(rise_delay=1n fall_delay=1n)")
            .arg(modelName, codeModel);
    }

    if (codeModel == "d_ram") {
        return QString(".model %1 d_ram(select_value=1 ic=2 read_delay=80n)")
            .arg(modelName);
    }

    return QString(".model %1 %2").arg(modelName, codeModel);
}

QString nativeLogicKeywordForCodeModel(const QString& codeModel) {
    if (codeModel == "d_and") return "AND";
    if (codeModel == "d_nand") return "NAND";
    if (codeModel == "d_or") return "OR";
    if (codeModel == "d_nor") return "NOR";
    if (codeModel == "d_xor") return "XOR";
    if (codeModel == "d_xnor") return "XNOR";
    if (codeModel == "d_buffer") return "BUF";
    if (codeModel == "d_inverter") return "NOT";
    if (codeModel == "d_dff") return "DFF";
    if (codeModel == "d_jkff") return "JKFF";
    if (codeModel == "d_tff") return "TFF";
    if (codeModel == "d_srff") return "SRFF";
    if (codeModel == "d_dlatch") return "DLATCH";
    if (codeModel == "d_srlatch") return "SRLATCH";
    if (codeModel == "d_tristate") return "TRISTATE";
    if (codeModel == "d_ram") return "RAM";
    return QString();
}

} // namespace



namespace {
bool xspiceModelUsesCollapsedInputVector(const QString& codeModel) {
    return codeModel == "d_and" || codeModel == "d_nand" ||
           codeModel == "d_or" || codeModel == "d_nor" ||
           codeModel == "d_xor" || codeModel == "d_xnor";
}
} // namespace





namespace {

QStringList splitTopLevelSpiceArgs(const QString& text) {
    QStringList args;
    QString current;
    int parenDepth = 0;
    int braceDepth = 0;

    for (QChar ch : text) {
        if (ch == ',' && parenDepth == 0 && braceDepth == 0) {
            args.append(current.trimmed());
            current.clear();
            continue;
        }
        if (ch == '(') ++parenDepth;
        else if (ch == ')' && parenDepth > 0) --parenDepth;
        else if (ch == '{') ++braceDepth;
        else if (ch == '}' && braceDepth > 0) --braceDepth;
        current += ch;
    }
    args.append(current.trimmed());
    return args;
}

int findMatchingParen(const QString& text, int openIndex) {
    if (openIndex < 0 || openIndex >= text.size() || text.at(openIndex) != '(') return -1;
    int depth = 0;
    for (int i = openIndex; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == '(') ++depth;
        else if (ch == ')') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return -1;
}






} // namespace



namespace {

QString fuzzyMatchPin(const QMap<QString, QString>& pins, const QString& subPinName) {
    const QString sub = subPinName.trimmed().toUpper();
    // Try exact match first
    if (pins.contains(sub)) return pins.value(sub);
    
    // Try with underscores removed
    QString simplified = sub;
    simplified.remove('_');
    if (pins.contains(simplified)) return pins.value(simplified);

    // Common Op-Amp patterns
    if (sub.contains("IN") && sub.contains("P")) {
        for (const QString& k : {"+", "IN+", "IN_P", "IP", "VIN+"}) 
            if (pins.contains(k)) return pins.value(k);
    }
    if (sub.contains("IN") && (sub.contains("N") || sub.contains("M"))) {
        for (const QString& k : {"-", "IN-", "IN_N", "IN_M", "IM", "VIN-"}) 
            if (pins.contains(k)) return pins.value(k);
    }
    if (sub.contains("OUT")) {
        for (const QString& k : {"OUT", "O", "VOUT"}) 
            if (pins.contains(k)) return pins.value(k);
    }
    if (sub.contains("VCC") || sub.contains("VDD") || sub.contains("VPP")) {
        for (const QString& k : {"V+", "VCC", "VDD", "VPP", "PVP"}) 
            if (pins.contains(k)) return pins.value(k);
    }
    if (sub.contains("VEE") || sub.contains("VSS") || sub.contains("VNN") || sub.contains("GND")) {
        for (const QString& k : {"V-", "VEE", "VSS", "VNN", "GND", "0"}) 
            if (pins.contains(k)) return pins.value(k);
    }
    
    return QString();
}

} // namespace



namespace {






QString formatPwlValueForNetlist(const QString& value, int maxLen = 120) {
    const QString v = value.trimmed();
    
    // If it contains FILE or WAVEFILE, we don't want to mess with wrapping 
    if (v.contains("FILE", Qt::CaseInsensitive) || v.contains("WAVEFILE", Qt::CaseInsensitive)) {
        return v;
    }

    if (!v.startsWith("PWL", Qt::CaseInsensitive)) return value;

    int closeIdx = v.lastIndexOf(')');
    if (closeIdx < 0) return value;

    QString tail = v.mid(closeIdx + 1).trimmed();
    QString inside = v.left(closeIdx + 1);
    int openIdx = inside.indexOf('(');
    if (openIdx < 0) return value;

    const QString head = "PWL(";
    const QString body = inside.mid(openIdx + 1, inside.length() - openIdx - 2);

    // Split body into tokens and filter out existing continuation characters
    QStringList rawTokens = body.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QStringList tokens;
    for (const QString& t : rawTokens) {
        if (t != "+") tokens << t;
    }
    
    if (tokens.isEmpty()) return value;

    QString result = head;
    
    // Try to keep time/value pairs on the same line
    for (int i = 0; i < tokens.size(); i += 2) {
        QString pair = tokens[i];
        if (i + 1 < tokens.size()) pair += " " + tokens[i+1];
        
        const int extra = pair.length() + 1;
        // Check if adding this pair would exceed maxLen
        // We look at the length of the LAST line of 'result'
        int lastLineLen = result.length() - result.lastIndexOf('\n') - 1;
        if (lastLineLen > 0 && lastLineLen + extra > maxLen) {
            result += "\n+ " + pair;
        } else {
            if (!result.endsWith("(") && !result.endsWith("+ ")) result += " ";
            result += pair;
        }
    }

    result += ")";
    if (!tail.isEmpty()) result += " " + tail;

    return result;
}

QStringList currentSaveVectorsForRef(const QString& spiceRef) {
    QString ref = spiceRef.trimmed();
    if (ref.isEmpty()) return {};

    // Extract first token (the reference) if the line contains nodes or values
    if (ref.contains(' ')) {
        ref = ref.split(QRegularExpression("\\s+")).at(0);
    }

    // Sanity check: paths or weird strings shouldn't be treated as SPICE references
    if (ref.contains('/') || ref.contains('\\') || ref.contains('.')) return {};

    const QChar prefix = ref.at(0).toUpper();
    switch (prefix.unicode()) {
    case 'R':
    case 'C':
    case 'L':
    case 'B':
        return { QString("@%1[i]").arg(ref) };
    case 'Q':
        // Return Collector, Base, and Emitter currents for BJT
        return { 
            QString("@%1[ic]").arg(ref), 
            QString("@%1[ib]").arg(ref), 
            QString("@%1[ie]").arg(ref) 
        };
    case 'D':
    case 'M':
    case 'J':
    case 'Z':
        // For Diodes, MOSFETs, JFETs - typically id (Drain/Diode current)
        // For MOSFETs we could also add ig, is if needed
        return { QString("@%1[id]").arg(ref) };
    default:
        return {};
    }
}

struct VoltageParasitics {
    QString value;
    QString rser;
    QString cpar;
};

static VoltageParasitics stripVoltageParasitics(const QString& value) {
    VoltageParasitics out{value, "", ""};
    QRegularExpression rserRe("\\bRser\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression cparRe("\\bCpar\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);

    auto rserMatch = rserRe.match(out.value);
    if (rserMatch.hasMatch()) {
        out.rser = rserMatch.captured(1).trimmed();
        out.value.remove(rserRe);
    }
    auto cparMatch = cparRe.match(out.value);
    if (cparMatch.hasMatch()) {
        out.cpar = cparMatch.captured(1).trimmed();
        out.value.remove(cparRe);
    }
    out.value = out.value.trimmed();
    return out;
}

} // namespace



namespace {

QString sanitizeDirectiveName(const QString& raw) {
    QString s = raw;
    s.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
    s.replace(QRegularExpression("_+"), "_");
    s.remove(QRegularExpression("^_+"));
    s.remove(QRegularExpression("_+$"));
    return s.isEmpty() ? QString("m") : s.left(40);
}

QString normalizeLtspiceMeasDirective(const QString& cmd, QStringList* warnings = nullptr) {
    QString out = cmd;

    if (!out.startsWith(".meas", Qt::CaseInsensitive)) return out;

    if (out.contains("I(", Qt::CaseInsensitive)) {
        if (warnings) {
            warnings->append(QString("LTspice-style .meas current reference detected: %1").arg(cmd.trimmed()));
            warnings->append(QString("Consider measuring source current via I(Vsense) or converting resistor current measurements manually for ngspice."));
        }
    }

    if (out.contains(" PARAM ", Qt::CaseInsensitive)) {
        if (warnings) {
            warnings->append(QString(".meas PARAM detected and passed through unchanged: %1").arg(cmd.trimmed()));
        }
    }

    if (out.contains(" FIND ", Qt::CaseInsensitive) && out.contains(" AT=", Qt::CaseInsensitive)) {
        if (warnings) {
            warnings->append(QString(".meas FIND ... AT= detected; verify LTspice/ngspice syntax compatibility: %1").arg(cmd.trimmed()));
        }
    }

    if (out.contains(QRegularExpression("\\bDERIV\\b", QRegularExpression::CaseInsensitiveOption))) {
        if (warnings) {
            warnings->append(QString(".meas DERIV detected; verify LTspice/ngspice derivative measurement syntax compatibility: %1").arg(cmd.trimmed()));
        }
    }

    if (out.contains(QRegularExpression("\\bTRIG\\b", QRegularExpression::CaseInsensitiveOption)) ||
        out.contains(QRegularExpression("\\bTARG\\b", QRegularExpression::CaseInsensitiveOption))) {
        if (warnings) {
            warnings->append(QString(".meas TRIG/TARG interval form detected; verify LTspice/ngspice compatibility: %1").arg(cmd.trimmed()));
        }
    }

    if (out.contains(QRegularExpression("\\b(RISE|FALL|CROSS)\\s*=\\s*(LAST|\\d+)", QRegularExpression::CaseInsensitiveOption))) {
        if (warnings) {
            warnings->append(QString(".meas RISE/FALL/CROSS qualifier detected; verify LTspice/ngspice event counting compatibility: %1").arg(cmd.trimmed()));
        }
    }

    if (out.contains(QRegularExpression("\\b(AVG|MAX|MIN|PP|RMS|INTEG)\\b", QRegularExpression::CaseInsensitiveOption))) {
        if (warnings) {
            warnings->append(QString(".meas interval reduction keyword detected and passed through unchanged: %1").arg(cmd.trimmed()));
        }
    }

    return out;
}

QString normalizeMeanDirective(const QString& cmd) {
    static const QRegularExpression re(
        "^\\s*\\.mean\\s+(?:(avg|max|min|rms)\\s+)?([^\\s]+)(?:\\s+from\\s*=\\s*([^\\s]+))?(?:\\s+to\\s*=\\s*([^\\s]+))?\\s*$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(cmd.trimmed());
    if (!m.hasMatch()) return cmd;

    const QString mode = m.captured(1).isEmpty() ? QString("avg") : m.captured(1).toLower();
    const QString signal = m.captured(2).trimmed();
    const QString from = m.captured(3).trimmed();
    const QString to = m.captured(4).trimmed();
    if (signal.isEmpty()) return cmd;

    const QString name = QString("mean_%1_%2_%3")
        .arg(mode, sanitizeDirectiveName(signal))
        .arg(QString::number(qHash(cmd.toLower()), 16));

    QString out = QString(".meas tran %1 %2 %3").arg(name, mode, signal);
    if (!from.isEmpty()) out += QString(" from=%1").arg(from);
    if (!to.isEmpty()) out += QString(" to=%1").arg(to);
    return out;
}

UserSpiceContentSummary summarizeUserSpiceText(const QString& text, const QString& projectDir) {
    UserSpiceContentSummary summary;

    static const QRegularExpression includeDirectiveRe(
        "^\\s*\\.(lib|inc|include)\\s+(?:\"([^\"]+)\"|(\\S+))",
        QRegularExpression::CaseInsensitiveOption);
    static const QSet<QString> analysisCards = {
        ".tran", ".ac", ".op", ".dc", ".noise", ".four", ".tf",
        ".disto", ".meas", ".step", ".sens", ".sp", ".net"
    };

    const QStringList lines = LtspiceRewriter::collapseSpiceContinuationLines(text);
    QSet<QString> analysisSeen;
    QMap<QString, int> modelSeen;
    QMap<QString, int> refSeen;
    QStringList subcktStack;
    int lineNo = 0;
    for (const QString& rawLine : lines) {
        ++lineNo;
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('*') || line.startsWith(';') || line.startsWith('#')) continue;

        if (line.startsWith('.')) {
            QString card;
            int firstSpace = line.indexOf(' ');
            if (firstSpace > 0) card = line.left(firstSpace).trimmed().toLower();
            else card = line.trimmed().toLower();

            if (analysisCards.contains(card)) {
                summary.hasExplicitAnalysisCard = true;
                if (analysisSeen.contains(card)) {
                    summary.warnings.append(QString("Duplicate analysis card %1 in directive block (line %2).")
                        .arg(card, QString::number(lineNo)));
                } else {
                    analysisSeen.insert(card);
                }
            }

            if (card == ".save") {
                summary.hasExplicitSaveDirective = true;
            }
            if (card == ".sp" || card == ".net") {
                summary.isSParameter = true;
            }

            if (card == ".net") {
                summary.hasNetDirective = true;
            }

            if (card == ".tran" && line.contains("startup", Qt::CaseInsensitive)) {
                summary.hasLtspiceStartup = true;
            }

            if (card == ".subckt") {
                const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    subcktStack.append(parts.at(1));
                }
            } else if (card == ".ends") {
                const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (subcktStack.isEmpty()) {
                    summary.warnings.append(QString(".ends has no matching .subckt (line %1).").arg(lineNo));
                } else {
                    const QString openName = subcktStack.takeLast();
                    if (parts.size() >= 2 && parts.at(1).compare(openName, Qt::CaseInsensitive) != 0) {
                        summary.warnings.append(QString(".ends %1 does not match open .subckt %2 (line %3).").arg(parts.at(1), openName, QString::number(lineNo)));
                    }
                }
            }

            const QRegularExpressionMatch includeMatch = includeDirectiveRe.match(line);
            if (includeMatch.hasMatch()) {
                const QString rawPath = includeMatch.captured(2).isEmpty()
                    ? includeMatch.captured(3)
                    : includeMatch.captured(2);
                const QString normalized = ModelInjector::normalizeIncludePathForNetlist(rawPath, projectDir);
                if (!normalized.isEmpty()) {
                    summary.declaredModelFiles.insert(normalized);
                }
            }

            if (card == ".model") {
                QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    const QString modelName = parts[1].toLower();
                    if (modelSeen.contains(modelName)) {
                        summary.warnings.append(QString("Duplicate .model %1 in directive block (lines %2 and %3).").arg(parts[1]).arg(modelSeen.value(modelName)).arg(lineNo));
                    } else {
                        modelSeen.insert(modelName, lineNo);
                    }
                    summary.declaredModelNames.insert(modelName);
                }

                if (line.contains(" D(", Qt::CaseInsensitive) &&
                    (line.contains("Ron=", Qt::CaseInsensitive) || line.contains("Roff=", Qt::CaseInsensitive) || line.contains("Vfwd=", Qt::CaseInsensitive))) {
                    summary.warnings.append(QString("LTspice-style diode model parameters detected in line %1; ngspice may reject Ron/Roff/Vfwd on .model D.").arg(lineNo));
                }
            }

            if (card == ".meas" && line.contains("I(", Qt::CaseInsensitive)) {
                summary.warnings.append(QString("Measurement current expression in line %1 may be LTspice-specific; ngspice is less reliable with I(R...) style expressions.").arg(lineNo));
            }

            if ((card == ".meas" || card == ".func" || card == ".param") && line.contains("table(", Qt::CaseInsensitive)) {
                summary.warnings.append(QString("table(...) detected in line %1; VioSpice will approximate inline point-pair forms for ngspice, but file/include-style forms may still differ.").arg(lineNo));
            }

            if (card == ".func") {
                summary.warnings.append(QString("LTspice .func detected in line %1; user-defined functions may rely on LTspice dynamic scoping, so verify ngspice compatibility when referenced inside subcircuits or with local .param overrides.").arg(lineNo));
            }

            if (card == ".step") {
                summary.warnings.append(QString("LTspice .step detected in line %1; this ngspice configuration reports .step as unimplemented, so VioSpice will omit it from the active netlist.").arg(lineNo));
            }

            if (card == ".four") {
                summary.warnings.append(QString("LTspice .four detected in line %1; verify Fourier-analysis compatibility and output behavior in ngspice.").arg(lineNo));
            }

            if (card == ".wave") {
                summary.warnings.append(QString("LTspice .wave detected in line %1; ngspice does not support LTspice WAV export directives.").arg(lineNo));
            }

            if ((card == ".param" || card == ".func") && line.contains("file=", Qt::CaseInsensitive)) {
                summary.warnings.append(QString("LTspice file= syntax detected in line %1; verify ngspice compatibility for file-driven expressions or sweeps.").arg(lineNo));
            }

            continue;
        }

        summary.hasElementCards = true;
        const bool emulateStartupOnLine = summary.hasLtspiceStartup && subcktStack.isEmpty();
        const QString rewrittenLine = LtspiceRewriter::rewriteLtspiceDirectiveLine(line, &summary.warnings, emulateStartupOnLine, projectDir);
        if (rewrittenLine.contains("if(", Qt::CaseInsensitive)) {
            summary.warnings.append(QString("LTspice-style if(...) expression remains in line %1 and may fail in ngspice.").arg(lineNo));
        }
        if (line.contains("table(", Qt::CaseInsensitive)) {
            summary.warnings.append(QString("table(...) detected in line %1; VioSpice will approximate inline point-pair forms for ngspice, but file/include-style forms may still differ.").arg(lineNo));
        }
        if (line.contains("wavefile=", Qt::CaseInsensitive)) {
            summary.warnings.append(QString("LTspice wavefile= source detected in line %1; ngspice compatibility for WAV-backed sources is not implemented in VioSpice.").arg(lineNo));
        }
        if (line.contains("chan=", Qt::CaseInsensitive) && line.contains("wavefile=", Qt::CaseInsensitive)) {
            summary.warnings.append(QString("LTspice chan= option for wavefile-backed sources detected in line %1; verify channel-selection compatibility manually.").arg(lineNo));
        }
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        const QString ref = parts.first().toUpper();
        if (refSeen.contains(ref)) {
            summary.warnings.append(QString("Duplicate element reference %1 in directive block (lines %2 and %3).").arg(parts.first()).arg(refSeen.value(ref)).arg(lineNo));
        } else {
            refSeen.insert(ref, lineNo);
        }
        summary.declaredElementRefs.insert(ref);

        const QChar prefix = ref.isEmpty() ? QChar() : ref.at(0);
        if ((prefix == 'V' || prefix == 'I') && parts.size() >= 2) {
            summary.drivenRailNets.insert(parts.at(1).trimmed().toUpper());
        }
    }

    if (!subcktStack.isEmpty()) {
        for (const QString& openSubckt : subcktStack) {
            summary.warnings.append(QString("Missing .ends for subcircuit %1.").arg(openSubckt));
        }
    }

    return summary;
}
}

SpiceNetlistGenerator::GeneratedNetlist SpiceNetlistGenerator::generate(QGraphicsScene* scene, const QString& projectDir, NetManager* /*netManager*/, const SimulationParams& paramsIn) {
    if (!scene) return { "* Missing scene\n", {} };

    SimulationParams params = paramsIn;
    // Fallback: If AC/S-Param mode and no source specified, pick first V source
    if ((params.type == AC || params.type == SParameter) && params.rfPort1Source.trimmed().isEmpty()) {
        for (QGraphicsItem* item : scene->items()) {
            if (auto* si = dynamic_cast<SchematicItem*>(item)) {
                if (si->itemType() == SchematicItem::VoltageSourceType && !si->reference().isEmpty()) {
                    params.rfPort1Source = si->reference();
                    break;
                }
            }
        }
    }

    qDebug() << "[SpiceNetlistGenerator] type=" << params.type << "rfPort1Source=" << params.rfPort1Source << "rfPort2Node=" << params.rfPort2Node << "rfZ0=" << params.rfZ0;

    QString netlist;
    netlist += "* viospice Automated Hierarchical SPICE Netlist\n";
    netlist += "* Generated: " + QDateTime::currentDateTime().toString(Qt::ISODate) + "\n";
    netlist += ".options ngbehavior=ltps\n";
    // Guardrail:
    // Keep a permissive deck-level default here. The actual native-logic switch
    // happens in SimulationManager before load, where `vicompat=lt` is applied
    // only for VioMATRIXC. A deck-only `.options vicompat=lt` is too late for
    // parse-time A-device rewriting and must not be relied on.
    netlist += ".options vicompat=all\n";
    if (params.type == AC || params.type == SParameter) {
        netlist += ".options savecurrents\n";
        netlist += ".save all v(all)\n";
        
        // Use .probe for branch currents - much more robust in VioMATRIXC/Ngspice
        for (QGraphicsItem* item : scene->items()) {
            if (auto* si = dynamic_cast<SchematicItem*>(item)) {
                QString ref = si->reference().trimmed();
                if (ref.isEmpty()) continue;
                
                if (ref.startsWith("R", Qt::CaseInsensitive) || 
                    ref.startsWith("C", Qt::CaseInsensitive) ||
                    ref.startsWith("L", Qt::CaseInsensitive)) {
                    netlist += QString(".probe i(%1)\n").arg(ref);
                } else if (ref.startsWith("D", Qt::CaseInsensitive)) {
                    netlist += QString(".probe i(%1)\n").arg(ref);
                } else if (ref.startsWith("Q", Qt::CaseInsensitive)) {
                    // For transistors, probe terminal currents
                    netlist += QString(".probe ic(%1) ib(%1) ie(%1)\n").arg(ref);
                }
            }
        }
    }

    // 0. Append SPICE Directives from schematic at the TOP 
    // This ensures .params and .model are defined before use
    netlist += "* Custom SPICE Directives\n";
    
    // --- Tuning Slider Parameters ---
    for (QGraphicsItem* item : scene->items()) {
        if (auto* slider = dynamic_cast<TuningSliderSymbolItem*>(item)) {
            QString ref = slider->reference().trimmed();
            if (!ref.isEmpty()) {
                netlist += QString(".param %1 = %2\n").arg(ref).arg(slider->currentValue());
            }
        }
    }
    QSet<QString> switchModelsAdded;
    QSet<QString> userDeclaredModelFiles;
    QSet<QString> userElementRefs;
    QSet<QString> userDrivenRailNets;
    QStringList directiveWarnings;
    bool hasExplicitSaveDirective = false;
    bool hasNetDirective = false;
    bool isSParameterDirective = false;
    bool hasExplicitAnalysisCard = false;
    bool hasUserElementCards = false;

    // Helper to determine if a card string matches the current simulation type
    auto cardMatchesType = [&](const QString& card, SimulationType type) {
        if (type == Transient && card == ".tran") return true;
        if (type == AC && card == ".ac") return true;
        if (type == SParameter && (card == ".sp" || card == ".net")) return true;
        if (type == DC && card == ".dc") return true;
        if (type == OP && card == ".op") return true;
        if (type == Noise && card == ".noise") return true;
        if (type == TF && card == ".tf") return true;
        if (type == Sens && card == ".sens") return true;
        if (type == Disto && card == ".disto") return true;
        return false;
    };

    for (QGraphicsItem* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            if (si->itemType() == SchematicItem::SpiceDirectiveType) {
                if (auto* dir = dynamic_cast<SchematicSpiceDirectiveItem*>(si)) {
                    QString cmd = dir->text().trimmed();
                    if (!cmd.isEmpty()) {
                        const UserSpiceContentSummary summary = summarizeUserSpiceText(cmd, projectDir);
                        userDeclaredModelFiles.unite(summary.declaredModelFiles);
                        switchModelsAdded.unite(summary.declaredModelNames);
                        userElementRefs.unite(summary.declaredElementRefs);
                        userDrivenRailNets.unite(summary.drivenRailNets);
                        directiveWarnings.append(summary.warnings);
                        hasExplicitAnalysisCard = hasExplicitAnalysisCard || summary.hasExplicitAnalysisCard;
                        hasUserElementCards = hasUserElementCards || summary.hasElementCards;
                        hasExplicitSaveDirective = hasExplicitSaveDirective || summary.hasExplicitSaveDirective;
                        hasNetDirective = hasNetDirective || summary.hasNetDirective;
                        isSParameterDirective = isSParameterDirective || summary.isSParameter;

                        const QStringList cmdLines = LtspiceRewriter::collapseSpiceContinuationLines(cmd);
                        int subcktDepth = 0;
                        for (const QString& rawCmdLine : cmdLines) {
                            const QString trimmedCmdLine = rawCmdLine.trimmed();
                            if (trimmedCmdLine.isEmpty()) {
                                netlist += "\n";
                                continue;
                            }

                            // Do NOT skip .net for SParameter - we need it!
                            // We only skip if we are sure we are replacing it later.


                            // Skip analysis directives that don't match the current simulation type
                            if (params.type != SParameter &&
                                (trimmedCmdLine.startsWith(".sp", Qt::CaseInsensitive) ||
                                 trimmedCmdLine.startsWith(".net", Qt::CaseInsensitive))) {
                                netlist += "* Skipped directive (not applicable to current analysis): " + trimmedCmdLine + "\n";
                                continue;
                            }
                            if (params.type == SParameter && trimmedCmdLine.startsWith(".ac", Qt::CaseInsensitive)) {
                                netlist += "* Skipped user .ac directive (auto-generated for SParameter analysis): " + trimmedCmdLine + "\n";
                                continue;
                            }

                            const bool emulateStartupOnLine = summary.hasLtspiceStartup && subcktDepth == 0;
                            QString lineToWrite = LtspiceRewriter::rewriteLtspiceDirectiveLine(trimmedCmdLine, &directiveWarnings, emulateStartupOnLine, projectDir);

                            // Resolve relative .include/.lib/.inc paths to absolute so ngspice can always
                            // find them regardless of its CWD.
                            {
                                static const QRegularExpression incLineRe(
                                    "^(\\s*\\.(?:include|lib|inc)\\s+)(?:\"([^\"]+)\"|(\\S+))\\s*$",
                                    QRegularExpression::CaseInsensitiveOption);
                                const QRegularExpressionMatch m = incLineRe.match(lineToWrite);
                                if (m.hasMatch()) {
                                    const QString keyword = m.captured(1);
                                    const QString rawPath = m.captured(2).isEmpty() ? m.captured(3) : m.captured(2);
                                    QFileInfo rfInfo(rawPath);
                                    if (!rfInfo.isAbsolute()) {
                                        const QString absPath = ModelInjector::normalizeIncludePathForNetlist(rawPath, projectDir);
                                        if (!absPath.isEmpty() && absPath != rawPath && QFileInfo::exists(absPath)) {
                                            lineToWrite = QString("%1\"%2\"").arg(keyword.trimmed() + " ", absPath);
                                            directiveWarnings.append(QString("Resolved relative include '%1' to '%2'.").arg(rawPath, absPath));
                                        }
                                    }
                                }
                            }

        if (trimmedCmdLine.startsWith(".mean", Qt::CaseInsensitive)) {
            const QString converted = normalizeMeanDirective(trimmedCmdLine);
            if (converted != trimmedCmdLine) {
                netlist += "* " + trimmedCmdLine + "\n";
                netlist += converted + "\n";
                LtspiceRewriter::updateSubcktDepthForLine(trimmedCmdLine, subcktDepth);
                continue;
            }
        }

        if (trimmedCmdLine.startsWith(".step", Qt::CaseInsensitive)) {
            netlist += "* " + trimmedCmdLine + "\n";
            netlist += "* LTspice .step omitted: this ngspice configuration reports .step as unimplemented\n";
            LtspiceRewriter::updateSubcktDepthForLine(trimmedCmdLine, subcktDepth);
            continue;
        }

                            if (trimmedCmdLine.startsWith(".meas", Qt::CaseInsensitive)) {
                                lineToWrite = normalizeLtspiceMeasDirective(lineToWrite, &directiveWarnings);
                            }

                            if (lineToWrite != trimmedCmdLine) {
                                netlist += "* LTspice rewrite: " + trimmedCmdLine + "\n";
                            }
                            netlist += lineToWrite + "\n";
                            LtspiceRewriter::updateSubcktDepthForLine(trimmedCmdLine, subcktDepth);
                        }
                    }
                }
            }
        }
    }

    if (params.type == OP && isSParameterDirective) {
        // Upgrade to SParameter mode if .sp/.net cards are present in Auto-Detect
        const_cast<SimulationParams&>(params).type = SParameter;
    }

    netlist += "\n";

    // 0.5 Collect model includes from symbols
    QSet<QString> includePaths;
    QSet<QString> libPaths;

    // 1. Get Flattened ECO Package (Components and Nets)
    qDebug() << "[SpiceNetlistGenerator] Generating ECO package...";
    ECOPackage pkg = NetlistGenerator::generateECOPackage(scene, projectDir, nullptr);
    qDebug() << "[SpiceNetlistGenerator] ECO package generated. Components:" << pkg.components.size() << "Nets:" << pkg.nets.size();
    
    // Evaluate connectivity
    ConnectivityResult connResult = ConnectivityEvaluator::evaluate(pkg, userDrivenRailNets);
    QMap<QString, QMap<QString, QString>>& componentPins = connResult.componentPins;
    qDebug() << "[SpiceNetlistGenerator] Pin mapping built.";

        // Use ComponentExtractor to get include/lib paths, embedded subcircuits, and model lines
    ComponentExtractor::ExtractionResult extraction = ComponentExtractor::extract(pkg, projectDir, switchModelsAdded);
    includePaths.unite(extraction.includePaths);
    libPaths.unite(extraction.libPaths);
    QMap<QString, QString> embeddedSubcircuits = extraction.embeddedSubcircuits;
    QStringList embeddedModelLines = extraction.embeddedModelLines;
    QStringList runtimeWarnings = extraction.runtimeWarnings;
    runtimeWarnings.append(connResult.runtimeWarnings);

    ModelInjector::inject(includePaths,
                          libPaths,
                          embeddedModelLines,
                          embeddedSubcircuits,
                          projectDir,
                          userDeclaredModelFiles,
                          netlist);

    // 3. Global Power Net Mapping for hidden pin auto-connection
    QMap<QString, QString>& powerNetMapping = connResult.powerNetMapping;

    // 4. Export components
    QMap<QString, QString>& powerNetVoltages = connResult.powerNetVoltages;
    QStringList savedCurrentVectors;
    QSet<QString> emittedRefs;
    QSet<QString>& digitalDrivenNets = connResult.digitalDrivenNets;
    NetlistManager::BridgeModels mixedModeBridgeModels;

    if (!digitalDrivenNets.isEmpty()) {
        netlist += "* Mixed-mode XSPICE bridges\n";
        netlist += QString(".model __viospice_adc_bridge adc_bridge(in_low=%1 in_high=%2 rise_delay=%3 fall_delay=%4)\n")
                       .arg(QString::number(mixedModeBridgeModels.adcLow, 'g', 12),
                            QString::number(mixedModeBridgeModels.adcHigh, 'g', 12),
                            QString::number(mixedModeBridgeModels.adcRiseDelay, 'g', 12),
                            QString::number(mixedModeBridgeModels.adcFallDelay, 'g', 12));
        netlist += QString(".model __viospice_dac_bridge dac_bridge(out_low=%1 out_high=%2 out_undef=%3 input_load=%4 t_rise=%5 t_fall=%6)\n")
                       .arg(QString::number(mixedModeBridgeModels.dacLow, 'g', 12),
                            QString::number(mixedModeBridgeModels.dacHigh, 'g', 12),
                            QString::number(mixedModeBridgeModels.dacUndef, 'g', 12),
                            QString::number(mixedModeBridgeModels.dacInputLoad, 'g', 12),
                            QString::number(mixedModeBridgeModels.dacRiseTime, 'g', 12),
                            QString::number(mixedModeBridgeModels.dacFallTime, 'g', 12));
        netlist += ".subckt __viospice_adc_wrap ANA DIG\n";
        netlist += "* XSPICE adc_bridge: analog wire into event-driven digital logic.\n";
        netlist += "A_ADC [ANA] [DIG] __viospice_adc_bridge\n";
        netlist += ".ends __viospice_adc_wrap\n";
        netlist += ".subckt __viospice_dac_wrap DIG ANA\n";
        netlist += "* XSPICE dac_bridge: event-driven digital logic into analog wire.\n";
        netlist += "A_DAC [DIG] [ANA] __viospice_dac_bridge\n";
        netlist += ".ends __viospice_dac_wrap\n\n";
    }

    for (const auto& comp : pkg.components) {
        if (comp.excludeFromSim) {
            netlist += "* Skipping " + comp.reference + " (Excluded from simulation)\n";
            continue;
        }

        QString ref = comp.reference;
        // Extract the original base reference (without hierarchy prefix) for SPICE
        // device-type heuristics. The prefixed ref (e.g. "SheetChild/V1") must not
        // be used for type detection because the first character can come from the
        // sheet name (e.g. 'S' from "SheetChild_V1") rather than the device prefix.
        QString baseRef = ref;
        {
            int lastSlash = ref.lastIndexOf('/');
            if (lastSlash >= 0) baseRef = ref.mid(lastSlash + 1);
        }
        QMap<QString, QString> pins = componentPins.value(ref);
        ref.replace("/", "_");
        const QString refKey = ref.trimmed().toUpper();
        
        const int type = comp.type;
        QString value = comp.value;
        const QString typeName = comp.typeName;

        // Power symbols often share the same '#' reference but represent different nets.
        // We handle them separately before the general duplicate check.
        if (type == SchematicItem::PowerType) {
            continue;
        }

        // Skip hierarchical port items — they are connection labels, not SPICE devices.
        // Their connectivity is already resolved through the net name mapping.
        if (type == SchematicItem::HierarchicalPortType) {
            continue;
        }

        if (userElementRefs.contains(refKey)) {
            runtimeWarnings.append(QString("Manual directive element %1 collides with schematic reference %2.").arg(ref, ref));
        }

        if (emittedRefs.contains(refKey)) {
            netlist += "* Skipping duplicate packaged unit " + ref + "\n";
            continue;
        }
        emittedRefs.insert(refKey);
        

        QString line;

        // Helper to ensure proper SPICE prefix without doubling it
        auto ensurePrefix = [](const QString& r, const QString& p) -> QString {
            if (r.startsWith(p, Qt::CaseInsensitive)) return r;
            return p + r;
        };

        // Determine SPICE prefix
        const bool isSevenSegmentDisplay = comp.typeName.contains("Segment Display", Qt::CaseInsensitive);
        const bool isVirtualTerminal = (comp.typeName == "VirtualTerminalInstrument" || comp.typeName == "Virtual Terminal");
        bool isInstrument = (comp.typeName == "OscilloscopeInstrument" ||
                             comp.typeName == "Oscilloscope Instrument" ||
                             comp.typeName == "VoltmeterInstrument" ||
                             comp.typeName == "Voltmeter (DC)" ||
                             comp.typeName == "Voltmeter (AC)" ||
                             comp.typeName == "AmmeterInstrument" ||
                             comp.typeName == "Ammeter (DC)" ||
                             comp.typeName == "Ammeter (AC)" ||
                             comp.typeName == "WattmeterInstrument" ||
                             comp.typeName == "Wattmeter" ||
                             comp.typeName == "FrequencyCounterInstrument" ||
                             comp.typeName == "Frequency Counter" ||
                             comp.typeName == "LogicProbeInstrument" ||
                             comp.typeName == "Logic Probe" ||
                             comp.typeName == "VirtualTerminalInstrument" ||
                             comp.typeName == "Virtual Terminal");

        if (isSevenSegmentDisplay) {
            // Visual-only instrument-style display:
            // can be interpreted as diode instances and break netlist parsing.
            netlist += QString("* Info: %1 is visual-only and is omitted from simulation netlist\n").arg(ref);
            continue;
        }

        if (isInstrument) {
            QStringList keys = pins.keys();
            std::sort(keys.begin(), keys.end());
            for (const QString& pk : keys) {
                QString node = pins[pk].replace(" ", "_");
                if (node.isEmpty() || node.toUpper().startsWith("NC")) continue;
                if (node == "0") continue; // No need to ground ground

                netlist += QString("R_%1_%2 %3 0 100Meg\n").arg(ref, pk, node);
            }

            // For Virtual Terminal with pending TX data, emit a PWL source on the TX pin
            if (isVirtualTerminal) {
                for (auto* scItem : scene->items()) {
                    if (auto* vt = dynamic_cast<VirtualTerminalItem*>(scItem)) {
                        if (vt->reference() == ref && vt->hasPendingTxData()) {
                            QString txNode = pins.value("2", "");
                            if (!txNode.isEmpty() && txNode != "0") {
                                QVector<QPair<double, double>> txWave = vt->pendingTxWaveform();
                                QStringList pwlPairs;
                                for (const auto& pt : txWave) {
                                    pwlPairs << QString::number(pt.first, 'g', 12) << QString::number(pt.second, 'g', 6);
                                }
                                netlist += QString("V_%1_TX %2 0 PWL(%3)\n").arg(ref, txNode, pwlPairs.join(" "));
                            }
                            vt->clearPendingTxWaveform();
                            break;
                        }
                    }
                }
            }

            continue;
        }

        // Determine SPICE prefix
        if (type == SchematicItem::ResistorType) line = ensurePrefix(ref, "R");
        else if (type == SchematicItem::CapacitorType) line = ensurePrefix(ref, "C");
        else if (type == SchematicItem::InductorType) line = ensurePrefix(ref, "L");
        else if (type == SchematicItem::DiodeType) line = ensurePrefix(ref, "D");
        else if (type == SchematicItem::TransistorType) line = ensurePrefix(ref, "Q");
        else if (type == SchematicItem::VoltageSourceType) {
            if (comp.value.trimmed().startsWith("V=", Qt::CaseInsensitive)) line = ensurePrefix(ref, "B");
            else line = ensurePrefix(ref, "V");
        }
        else if (type == SchematicItem::SmartSignalType) {
            line = ensurePrefix(ref, "V"); // Controlled by FluxScript JIT
            value = "0"; // Initial value
        }
        else if (typeName == "LogicToggle") {
            line = ensurePrefix(ref, "V"); // Acts as a Voltage Source
            if (!pins.contains("2")) {
                pins.insert("2", "0"); // Inject explicit GND pin
            }
        }
        else if (typeName == "XspiceBlock") {
            line = ensurePrefix(ref, "A"); // XSPICE A-device block
        }
        else if (ConnectivityEvaluator::isXspiceLogicComponent(comp.spiceModel.trimmed().isEmpty() ? comp.value.trimmed() : comp.spiceModel.trimmed(),
                                        comp.typeName,
                                        ref)) {
            line = ensurePrefix(ref, "A"); // XSPICE A-device
        }
        else if (typeName == "SystemVerilogBlock") {
            // SystemVerilog blocks use native XSPICE A-devices with per-output JIT functions.
            // Format: A_{ref}_{pin} [in1 in2...] outNet viospice_jit_model_{ref}_{pin}
            //         .model viospice_jit_model_{ref}_{pin} viospice_jit (jit_id="{ref}_{pin}")
            QString svPath = comp.value;
            if (svPath.isEmpty() && comp.extraProperties.contains("systemVerilogFile"))
                svPath = comp.extraProperties["systemVerilogFile"];

            QStringList inputPins;
            QStringList outputPins;
            QFile svFile(svPath);
            if (svFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString svText = QString::fromUtf8(svFile.readAll());
                QRegularExpression portRe(R"((input|output)\s+(logic\s+)?(\w+))",
                    QRegularExpression::CaseInsensitiveOption);
                for (auto m = portRe.globalMatch(svText); m.hasNext(); ) {
                    auto match = m.next();
                    QString dir = match.captured(1).toLower();
                    QString pname = match.captured(3);
                    if (dir == "input") inputPins.append(pname);
                    else outputPins.append(pname);
                }
            }

            // Build input net vector in declaration order
            QStringList inNets;
            for (const QString& pin : inputPins) {
                QString net = pins.value(pin);
                if (net.isEmpty()) net = "0";
                net.replace(" ", "_");
                inNets << net;
            }
            QString inVector = inNets.isEmpty() ? "0" : "[" + inNets.join(" ") + "]";

            // Generate A-device + model for each output pin
            for (const QString& pin : outputPins) {
                QString netName = pins.value(pin);
                if (netName.isEmpty()) continue;
                netName.replace(" ", "_");
                QString outId = QString("%1_%2").arg(ref, pin.toUpper());
                netlist += QString("A_%1 %2 %3 viospice_jit_model_%1\n").arg(outId, inVector, netName);
                netlist += QString(".model viospice_jit_model_%1 viospice_jit (jit_id=\"%1\")\n").arg(outId);
            }

            continue; // No main netlist line for synthetic SV blocks
        }
        else if (typeName == "AdcBridge") {
            QString inNet = pins.value("In");
            QString outNet = pins.value("Out");
            if (inNet.isEmpty()) inNet = "0";
            if (outNet.isEmpty()) outNet = "0";
            inNet.replace(" ", "_");
            outNet.replace(" ", "_");
            QString modelName = QString("adc_%1").arg(ref);
            double inLow = comp.paramExpressions.value("in_low", "0.1").toDouble();
            double inHigh = comp.paramExpressions.value("in_high", "0.9").toDouble();
            double riseD = comp.paramExpressions.value("rise_delay", "1e-9").toDouble();
            double fallD = comp.paramExpressions.value("fall_delay", "1e-9").toDouble();
            netlist += QString("A_%1 %2 %3 %4\n").arg(ref, inNet, outNet, modelName);
            netlist += QString(".model %4 adc_bridge(in_low=%1 in_high=%2 rise_delay=%3 fall_delay=%4)\n")
                .arg(inLow).arg(inHigh).arg(riseD).arg(fallD).arg(modelName);
            continue;
        }
        else if (typeName == "DacBridge") {
            QString inNet = pins.value("In");
            QString outNet = pins.value("Out");
            if (inNet.isEmpty()) inNet = "0";
            if (outNet.isEmpty()) outNet = "0";
            inNet.replace(" ", "_");
            outNet.replace(" ", "_");
            QString modelName = QString("dac_%1").arg(ref);
            double outLow = comp.paramExpressions.value("out_low", "0.0").toDouble();
            double outHigh = comp.paramExpressions.value("out_high", "5.0").toDouble();
            double outUndef = comp.paramExpressions.value("out_undef", "2.5").toDouble();
            double inputLoad = comp.paramExpressions.value("input_load", "1e-12").toDouble();
            double tRise = comp.paramExpressions.value("t_rise", "1e-9").toDouble();
            double tFall = comp.paramExpressions.value("t_fall", "1e-9").toDouble();
            netlist += QString("A_%1 %2 %3 %4\n").arg(ref, inNet, outNet, modelName);
            netlist += QString(".model %4 dac_bridge(out_low=%1 out_high=%2 out_undef=%3 input_load=%4 t_rise=%5 t_fall=%6)\n")
                .arg(outLow).arg(outHigh).arg(outUndef).arg(inputLoad).arg(tRise).arg(tFall).arg(modelName);
            continue;
        }
        else if (typeName == "AnalogFunction") {
            QString inNet = pins.value("In");
            QString outNet = pins.value("Out");
            if (inNet.isEmpty()) inNet = "0";
            if (outNet.isEmpty()) outNet = "0";
            inNet.replace(" ", "_");
            outNet.replace(" ", "_");

            QString funcType = comp.paramExpressions.value("functionType", "gain");
            QString modelName = QString("%1_%2").arg(funcType, ref);

            // Build .model param list from paramExpressions matching the function type
            QStringList modelParams;
            auto addParam = [&](const QString& key, double def) {
                double v = comp.paramExpressions.value(key, QString::number(def)).toDouble();
                modelParams << QString("%1=%2").arg(key).arg(v);
            };

            if (funcType == "gain") {
                addParam("gain", 1.0);
                addParam("in_offset", 0.0);
                addParam("out_offset", 0.0);
            } else if (funcType == "hyst") {
                addParam("in_low", 0.0);
                addParam("in_high", 1.0);
                addParam("hyst", 0.1);
                addParam("out_lower_limit", 0.0);
                addParam("out_upper_limit", 5.0);
            } else if (funcType == "int") {
                addParam("gain", 1.0);
                addParam("in_offset", 0.0);
                addParam("out_lower_limit", -1e6);
                addParam("out_upper_limit", 1e6);
            } else if (funcType == "d_dt") {
                addParam("gain", 1.0);
                addParam("out_offset", 0.0);
            } else if (funcType == "limit") {
                addParam("gain", 1.0);
                addParam("in_offset", 0.0);
                addParam("out_lower_limit", -1.0);
                addParam("out_upper_limit", 1.0);
                addParam("limit_range", 0.01);
            } else if (funcType == "slew") {
                addParam("rise_slope", 1e-9);
                addParam("fall_slope", 1e-9);
            }

            netlist += QString("A_%1 %2 %3 %4\n").arg(ref, inNet, outNet, modelName);
            netlist += QString(".model %1 %2(%3)\n").arg(modelName, funcType, modelParams.join(" "));
            continue;
        }
        else if (typeName == "MagneticCore") {
            QString plusNet = pins.value("PLUS", "0").replace(" ", "_");
            QString minusNet = pins.value("MINUS", "0").replace(" ", "_");
            QString modelName = QString("core_%1").arg(ref);
            QStringList params;
            auto addP = [&](const QString& key, const QString& val) {
                params << QString("%1=%2").arg(key, val);
            };
            addP("area", QString::number(comp.paramExpressions.value("area", "1e-4").toDouble(), 'g', 12));
            addP("length", QString::number(comp.paramExpressions.value("length", "1e-2").toDouble(), 'g', 12));
            int mode = comp.paramExpressions.value("mode", "1").toInt();
            addP("mode", QString::number(mode));
            if (mode == 1) {
                addP("H_array", "[" + comp.paramExpressions.value("H_array", "-200 -100 100 200") + "]");
                addP("B_array", "[" + comp.paramExpressions.value("B_array", "-1.26 -0.63 0.63 1.26") + "]");
                addP("input_domain", QString::number(comp.paramExpressions.value("input_domain", "0.01").toDouble(), 'g', 12));
                addP("fraction", comp.paramExpressions.value("fraction", "TRUE"));
            } else {
                addP("in_low", QString::number(comp.paramExpressions.value("in_low", "-1.0").toDouble(), 'g', 12));
                addP("in_high", QString::number(comp.paramExpressions.value("in_high", "1.0").toDouble(), 'g', 12));
                addP("hyst", QString::number(comp.paramExpressions.value("hyst", "0.1").toDouble(), 'g', 12));
                addP("out_lower_limit", QString::number(comp.paramExpressions.value("out_lower_limit", "-1.0").toDouble(), 'g', 12));
                addP("out_upper_limit", QString::number(comp.paramExpressions.value("out_upper_limit", "1.0").toDouble(), 'g', 12));
            }
            netlist += QString("A_%1 %2 %3 %4\n").arg(ref, plusNet, minusNet, modelName);
            netlist += QString(".model %1 core(%2)\n").arg(modelName, params.join(" "));
            continue;
        }
        else if (typeName == "Lcouple") {
            QString lPlus = pins.value("L+", "0").replace(" ", "_");
            QString lMinus = pins.value("L-", "0").replace(" ", "_");
            QString mmfPlus = pins.value("MMF+", "0").replace(" ", "_");
            QString mmfMinus = pins.value("MMF-", "0").replace(" ", "_");
            QString modelName = QString("lcouple_%1").arg(ref);
            double turns = comp.paramExpressions.value("num_turns", "100").toDouble();
            netlist += QString("A_%1 (%2 %3) (%4 %5) %6\n").arg(ref, lPlus, lMinus, mmfPlus, mmfMinus, modelName);
            netlist += QString(".model %1 lcouple(num_turns=%2)\n").arg(modelName, QString::number(turns, 'g', 12));
            continue;
        }
        else line = ensurePrefix(ref, "X"); // Subcircuit or generic
        // Fallback: if we don't know the type but reference has a known prefix,
        // use the reference as-is to avoid invalid X-lines.
        // Use the original (unprefixed) baseRef for prefix detection so that
        // V1 in a child sheet is still identified by its 'V' prefix.
        if (line.startsWith("X") && !baseRef.isEmpty()) {
            const QChar p = baseRef.at(0).toUpper();
            const QString known = "RCLVIDQMBEGFHJZ";
            if (known.contains(p)) {
                line = ensurePrefix(ref, QString(p));
            }
        }
        const bool isADevice = line.startsWith("A", Qt::CaseInsensitive);
        const QString normalizedLogicCodeModel =
            isADevice ? ConnectivityEvaluator::normalizeXspiceModelAlias(comp.spiceModel.trimmed().isEmpty() ? comp.value.trimmed() : comp.spiceModel.trimmed(),
                                                  comp.typeName)
                      : QString();
        const bool isNativeLogicADevice = isADevice && ConnectivityEvaluator::usesNativeLogicADevice(normalizedLogicCodeModel);

        // ── XSPICE Behavioral/Digital Block & AVR Microcontroller Co-Simulation ──
        if (XSpiceBlockTranslator::translate(comp, pins, projectDir, digitalDrivenNets, netlist, runtimeWarnings, pkg.nets)) {
            continue;
        }

        // --- SPICE Mapper Logic ---
        value = comp.value;
        if (!comp.spiceModel.isEmpty()) value = comp.spiceModel;

        // Standardize WAVEFILE and CHAN to space-separated syntax for better parser compatibility
        if (value.contains("WAVEFILE", Qt::CaseInsensitive)) {
            // Resolve relative path if needed
            QRegularExpression reFile(R"(WAVEFILE\s*=\s*\"([^\"]+)\")", QRegularExpression::CaseInsensitiveOption);
            auto match = reFile.match(value);
            if (match.hasMatch()) {
                QString rawPath = match.captured(1);
                QString targetPath = rawPath;
                QFileInfo fi(rawPath);
                if (!fi.isAbsolute() && !projectDir.isEmpty()) {
                    targetPath = QDir(projectDir).absoluteFilePath(rawPath);
                }
                value = QString("WAVEFILE \"%1\"").arg(targetPath);
            }

            QRegularExpression reChan(R"(CHAN\s*=\s*(\d+))", QRegularExpression::CaseInsensitiveOption);
            auto matchChan = reChan.match(comp.value); // Check original comp.value if model didn't have it
            if (!matchChan.hasMatch()) matchChan = reChan.match(value);
            if (matchChan.hasMatch()) {
                value += " CHAN " + matchChan.captured(1);
            }
        } else if (value.contains("FILE=", Qt::CaseInsensitive)) {
            // Resolve relative paths for other FILE= references
            QRegularExpression reFile(R"(FILE\s*=\s*\"([^\"]+)\")", QRegularExpression::CaseInsensitiveOption);
            auto match = reFile.match(value);
            if (match.hasMatch()) {
                QString rawPath = match.captured(1);
                QFileInfo fi(rawPath);
                if (!fi.isAbsolute() && !projectDir.isEmpty()) {
                    QString absPath = QDir(projectDir).absoluteFilePath(rawPath);
                    value.replace(rawPath, absPath);
                }
            }
        }

        value = LtspiceRewriter::inlinePwlFileIfNeeded(value, projectDir, nullptr);
        value = formatPwlValueForNetlist(value);
        QString instanceSuffix;
        QStringList nodes;
        const SimSubcircuit* activeSub = nullptr;

        // Find Symbol definition to check for custom mapping
        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(comp.typeName);
        if (sym) {
            // --- AUTO-CONNECT MISSING PINS (Hidden or unplaced Units) ---
            // Use existing 'pins' from outer scope

            // Check symbol definition for pins not present on the schematic
            const auto& symPins = sym->primitives();
            for (const auto& prim : symPins) {
                if (prim.type == Flux::Model::SymbolPrimitive::Pin) {
                    const QString pNum = QString::number(prim.data.value("number").toInt());
                    if (!pins.contains(pNum)) {
                        // Candidate for auto-connection
                        QString pName = prim.data.value("name").toString().toUpper();
                        if (pName == "VCC" || pName == "V+" || pName == "VDD") pins[pNum] = powerNetMapping.value("VCC", "VCC");
                        else if (pName == "VEE" || pName == "V-" || pName == "VSS") pins[pNum] = powerNetMapping.value("VEE", "VEE");
                        else if (pName == "GND" || pName == "0") pins[pNum] = powerNetMapping.value("GND", "0");
                        else pins[pNum] = "0"; // Default fallback
                    }
                }
            }
            
            // Ensure we use the updated mapping
            if (!sym->spiceModelName().isEmpty() && comp.spiceModel.isEmpty()) value = sym->spiceModelName();

            if (!sym->modelName().isEmpty() && comp.spiceModel.isEmpty()) {
                const QString mn = sym->modelName();
                const bool isX = line.startsWith("X");
                const bool isD = line.startsWith("D");
                const bool isQ = line.startsWith("Q");
                const bool isM = line.startsWith("M");

                if (isX || isD || isQ || isM || isADevice) {
                    // For subcircuits and complex devices, sym->modelName() is usually the SPICE model/subckt name.
                    // Only skip if it's just the single-letter prefix (legacy placeholder).
                    if (mn.length() > 1 || mn.toLower() != line.left(1).toLower()) {
                        value = mn;
                    }
                }
            }

            if (!sym->modelPath().isEmpty()) {
                const QString resolved = ComponentExtractor::resolveModelPath(sym->modelPath(), projectDir);
                if (resolved.isEmpty() || !QFileInfo::exists(resolved)) {
                    netlist += QString("* Warning: Model file '%1' not found for %2\n").arg(sym->modelPath(), ref);
                }
            }

            if (!sym->modelName().isEmpty()) {
                // Skip warning if modelName is just the device prefix letter
                const QString mn = sym->modelName();
                bool isPrefixOnly = (mn.length() == 1 && mn.toLower() == line.left(1).toLower());
                if (!isPrefixOnly) {
                    const SimModel* mdl = ModelLibraryManager::instance().findModel(mn);
                    const SimSubcircuit* sub = ModelLibraryManager::instance().findSubcircuit(mn);
                    if (!mdl && !sub) {
                        netlist += QString("* Warning: Model '%1' not found for %2\n").arg(mn, ref);
                    } else if (sub) {
                        int symPinsCount = sym->connectionPoints().size();
                        auto mappingPins = sym->spiceNodeMapping();
                        if (!comp.pinPadMapping.isEmpty()) {
                            mappingPins.clear();
                            for (auto it = comp.pinPadMapping.constBegin(); it != comp.pinPadMapping.constEnd(); ++it) {
                                bool ok = false;
                                const int symbolPin = it.key().toInt(&ok);
                                if (ok && !it.value().trimmed().isEmpty()) {
                                    mappingPins.insert(symbolPin, it.value().trimmed());
                                }
                            }
                        }
                        if (!mappingPins.isEmpty() && line.startsWith("X", Qt::CaseInsensitive)) {
                            // For subcircuits with explicit mapping, compare against mapped simulation pins
                            // instead of raw drawable symbol pins (which may contain extra NC/alt-unit pins).
                            symPinsCount = mappingPins.size();
                        }
                        const int subPins = static_cast<int>(sub->pinNames.size());
                        if (symPinsCount > 0 && subPins > 0 && symPinsCount != subPins) {
                            netlist += QString("* Warning: Pin count mismatch for %1 (symbol %2 vs subckt %3)\n")
                                               .arg(ref)
                                               .arg(symPinsCount)
                                               .arg(subPins);
                        }
                    }
                    if (!activeSub && sub) activeSub = sub;
                }
            }
            
            QMap<int, QString> mapping = sym->spiceNodeMapping();
            if (!comp.pinPadMapping.isEmpty()) {
                mapping.clear();
                for (auto it = comp.pinPadMapping.constBegin(); it != comp.pinPadMapping.constEnd(); ++it) {
                    bool ok = false;
                    const int symbolPin = it.key().toInt(&ok);
                    if (ok && !it.value().trimmed().isEmpty()) {
                        mapping.insert(symbolPin, it.value().trimmed());
                    }
                }
            }
            if (!mapping.isEmpty()) {
                // KiCad Sim.Pins mapping is typically: symbolPinNumber -> subcktPinName.
                // If we know the active subckt signature, emit nodes in its formal pin order.
                if (line.startsWith("X", Qt::CaseInsensitive)) {
                    if (!activeSub && !value.trimmed().isEmpty()) {
                        activeSub = ModelLibraryManager::instance().findSubcircuit(value.trimmed());
                    }

                    if (activeSub) {
                        QMap<QString, int> subPinToSymbolPin;
                        for (auto it = mapping.constBegin(); it != mapping.constEnd(); ++it) {
                            subPinToSymbolPin.insert(it.value().trimmed().toUpper(), it.key());
                        }

                        // Build reverse mapping: symbol pin number -> symbol pin name
                        // pins map is keyed by pin NAME, not pin number
                        QMap<int, QString> symbolPinNoToName;
                        for (auto it = pins.constBegin(); it != pins.constEnd(); ++it) {
                            bool ok = false;
                            int pinNum = it.key().toInt(&ok);
                            if (ok) {
                                symbolPinNoToName[pinNum] = it.key();
                            }
                        }

                        for (const std::string& sp : activeSub->pinNames) {
                            const QString subPin = QString::fromStdString(sp).trimmed();
                            QString net = "0";

                            const int symbolPinNo = subPinToSymbolPin.value(subPin.toUpper(), -1);
                            if (symbolPinNo >= 0) {
                                // Look up by pin name (the key in pins map), not by number
                                const QString symbolPinName = symbolPinNoToName.value(symbolPinNo, QString::number(symbolPinNo));
                                net = pins.value(symbolPinName, QString());
                            }
                            // Fallbacks for symbol sets that key by pin names/tokens.
                            if (net.isEmpty()) net = fuzzyMatchPin(pins, subPin);
                            if (net.isEmpty()) net = pins.value(subPin, QString());
                            if (net.isEmpty()) net = "0";

                            nodes.append(net.replace(" ", "_"));
                        }
                    } else {
                        // activeSub not found — fall back to mapping
                        // pins map is keyed by symbol pin NUMBER ("1","2",...)
                        // mapping values are subcircuit node names which may differ
                        QList<int> sortedIndices = mapping.keys();
                        std::sort(sortedIndices.begin(), sortedIndices.end());
                        for (int idx : sortedIndices) {
                            // Look up by symbol pin number directly
                            QString net = pins.value(QString::number(idx), "0").replace(" ", "_");
                            nodes.append(net);
                        }
                    }
                } else {
                    // Non-subcircuit: look up by symbol pin number directly
                    QList<int> sortedIndices = mapping.keys();
                    std::sort(sortedIndices.begin(), sortedIndices.end());
                    for (int idx : sortedIndices) {
                        QString net = pins.value(QString::number(idx), "0").replace(" ", "_");
                        nodes.append(net);
                    }
                }
            }
        }
        
        if (nodes.isEmpty() && type == SchematicItem::TransistorType) {
            // Hardcoded mapping for TransistorItem if no symbol definition provides it
            // TransistorItem pins: 0=B/G, 1=C/D, 2=E/S
            // ngspice expects: BJT=C B E, MOSFET=D G S
            QString b_g = pins.value("B", pins.value("G", "0")).replace(" ", "_");
            QString c_d = pins.value("C", pins.value("D", "0")).replace(" ", "_");
            QString e_s = pins.value("E", pins.value("S", "0")).replace(" ", "_");
            
            nodes.append(c_d);
            nodes.append(b_g);
            nodes.append(e_s);
        }

        auto isEffectivelyDisconnectedNode = [&](const QString& nodeName) {
            const QString node = nodeName.trimmed();
            return node.isEmpty() || node == "0" || node.startsWith("NC_", Qt::CaseInsensitive);
        };

        if (nodes.isEmpty()) {
            // Default: Fallback to natural sorting of pins
            QStringList sortedKeys = pins.keys();
            std::sort(sortedKeys.begin(), sortedKeys.end(), XSpiceBlockTranslator::naturalPinLessThan);
            
            if (sortedKeys.isEmpty()) {
                netlist += "* Skipping " + ref + " (no connections)\n";
                continue;
            }

            // XSPICE A-device vector grouping: [in1 in2 ...] out
            // For the built-in digital gates we target here, inputs may be vectorized
            // and bus-style pins like A0..A3 or D[0]..D[7] are collapsed into
            // a single XSPICE vector token. Scalar outputs stay scalar; grouped
            // bus outputs become [out0 out1 ...] only when the symbol actually
            // exposes multiple indexed outputs.
            if (isADevice) {
                QList<XspicePinAssignment> assignments;
                assignments.reserve(sortedKeys.size());
                int order = 0;
                for (const QString& pk : sortedKeys) {
                    QString net = pins[pk].replace(" ", "_");
                    if (net.isEmpty()) net = "NC_" + ref;
                    const QString heuristicPinName = ConnectivityEvaluator::pinNameForHeuristics(sym, pk);

                    bool hasDomainMetadata = false;
                    const NodeType domain = ConnectivityEvaluator::pinDomainFromMetadata(sym, pk, &hasDomainMetadata);
                    bool hasDirectionMetadata = false;
                    const NetlistManager::PinDirection direction = ConnectivityEvaluator::pinDirectionFromMetadata(sym, pk, &hasDirectionMetadata);
                    const bool isExplicitDigitalInput = hasDomainMetadata && domain == NodeType::DIGITAL_EVENT &&
                                                        hasDirectionMetadata && direction == NetlistManager::PinDirection::INPUT;
                    const bool shouldTreatAsInput = isExplicitDigitalInput ||
                                                    (!hasDirectionMetadata && isLikelyLogicInputPinName(heuristicPinName));

                    if (shouldTreatAsInput && !isNativeLogicADevice) {
                        if (!digitalDrivenNets.contains(net)) {
                            const QString bridgedNet = QString("__MM_ADC_%1_%2").arg(XSpiceBlockTranslator::sanitizeMixedModeToken(ref), XSpiceBlockTranslator::sanitizeMixedModeToken(pk));
                            netlist += XSpiceBlockTranslator::mixedModeAdcBridgeLine(ref, pk, net, bridgedNet) + "\n";
                            runtimeWarnings.append(QString("Inserted adc_bridge on %1.%2 so analog net %3 can drive XSPICE digital input.").arg(ref, pk, net));
                            net = bridgedNet;
                        }
                    }

                    XspicePinAssignment assignment;
                    assignment.pinIdentifier = pk;
                    assignment.pinName = heuristicPinName;
                    assignment.netName = net;
                    assignment.isInput = shouldTreatAsInput;
                    assignment.order = order++;
                    assignment.vector = vectorPinInfo(sym, pk, heuristicPinName);
                    assignments.append(assignment);
                }

                bool hasInput = false;
                for (const XspicePinAssignment& assignment : assignments) {
                    if (assignment.isInput) {
                        hasInput = true;
                        break;
                    }
                }
                if (!hasInput && assignments.size() >= 2) {
                    for (int i = 0; i < assignments.size() - 1; ++i) {
                        assignments[i].isInput = true;
                    }
                    assignments.last().isInput = false;
                }

                if (isNativeLogicADevice) {
                    QStringList nativeNodes(8, QStringLiteral("0"));
                    QList<XspicePinAssignment> inputs;
                    QList<XspicePinAssignment> outputs;
                    for (const XspicePinAssignment& assignment : assignments) {
                        if (assignment.isInput) inputs.append(assignment);
                        else outputs.append(assignment);
                    }

                    auto upperName = [](const XspicePinAssignment& assignment) {
                        return assignment.pinName.trimmed().toUpper();
                    };
                    auto takeByNames = [upperName](QList<XspicePinAssignment>& pool, std::initializer_list<const char*> names) -> QString {
                        for (const char* rawName : names) {
                            const QString wanted = QString::fromLatin1(rawName);
                            for (int i = 0; i < pool.size(); ++i) {
                                if (upperName(pool[i]) == wanted) {
                                    const QString net = pool.takeAt(i).netName;
                                    return net.isEmpty() ? QStringLiteral("0") : net;
                                }
                            }
                        }
                        return QString();
                    };
                    auto takeFirst = [](QList<XspicePinAssignment>& pool) -> QString {
                        if (pool.isEmpty()) return QString();
                        const QString net = pool.takeFirst().netName;
                        return net.isEmpty() ? QStringLiteral("0") : net;
                    };
                    auto placeIf = [&](int index, const QString& net) {
                        if (index >= 0 && index < nativeNodes.size() && !net.isEmpty()) nativeNodes[index] = net;
                    };

                    if (normalizedLogicCodeModel == "d_dff") {
                        placeIf(0, takeByNames(inputs, {"D"}));
                        placeIf(1, takeByNames(inputs, {"CLK", "CLOCK", "CK", "C"}));
                    } else if (normalizedLogicCodeModel == "d_jkff") {
                        placeIf(0, takeByNames(inputs, {"J"}));
                        placeIf(1, takeByNames(inputs, {"K"}));
                        placeIf(2, takeByNames(inputs, {"CLK", "CLOCK", "CK", "C"}));
                    } else if (normalizedLogicCodeModel == "d_tff") {
                        placeIf(0, takeByNames(inputs, {"T"}));
                        placeIf(1, takeByNames(inputs, {"CLK", "CLOCK", "CK", "C"}));
                    } else if (normalizedLogicCodeModel == "d_srff" || normalizedLogicCodeModel == "d_srlatch") {
                        placeIf(0, takeByNames(inputs, {"S", "SET"}));
                        placeIf(1, takeByNames(inputs, {"R", "RESET"}));
                    } else if (normalizedLogicCodeModel == "d_dlatch") {
                         placeIf(0, takeByNames(inputs, {"D", "DATA"}));
                         placeIf(1, takeByNames(inputs, {"CLK", "CLOCK", "CK", "C", "EN", "ENABLE", "G", "GATE"}));
                    } else if (normalizedLogicCodeModel == "d_ram") {
                        int slot = 0;
                        while (slot < 5 && !inputs.isEmpty()) nativeNodes[slot++] = takeFirst(inputs);
                    } else {
                        int slot = 0;
                        while (slot < 5 && !inputs.isEmpty()) nativeNodes[slot++] = takeFirst(inputs);
                    }

                    QList<XspicePinAssignment> outputPool = assignments;
                    for (int i = outputPool.size() - 1; i >= 0; --i) {
                        if (outputPool[i].isInput) outputPool.removeAt(i);
                    }
                    QString q = takeByNames(outputPool, {"Q", "Y", "OUT"});
                    QString nq = takeByNames(outputPool, {"NQ", "QN", "QB", "OUTB", "YB"});
                    if (q.isEmpty()) q = takeFirst(outputPool);
                    if (nq.isEmpty()) nq = takeFirst(outputPool);
                    placeIf(6, q);
                    placeIf(7, nq);

                    nodes = nativeNodes;
                } else {
                    const QString pendingCodeModel = ConnectivityEvaluator::normalizeXspiceModelAlias(value, comp.typeName);
                    nodes = buildXspiceNodeTokens(assignments, xspiceModelUsesCollapsedInputVector(pendingCodeModel));
                }
            } else {
                for (const QString& pk : sortedKeys) {
                    QString net = pins[pk];
                    if (net.isEmpty()) net = "NC_" + ref;
                    nodes.append(net.replace(" ", "_"));
                }
            }
        }

        const bool isSemiconductorInstance =
            line.startsWith("D", Qt::CaseInsensitive) ||
            line.startsWith("M", Qt::CaseInsensitive) ||
            line.startsWith("Q", Qt::CaseInsensitive);
        if (isSemiconductorInstance && !nodes.isEmpty()) {
            bool allDisconnected = true;
            for (const QString& node : nodes) {
                if (!isEffectivelyDisconnectedNode(node)) {
                    allDisconnected = false;
                    break;
                }
            }
            if (allDisconnected) {
                netlist += "* Skipping " + ref + " (unconnected live duplicate)\n";
                continue;
            }
        }

        const QStringList currentSaveVectors = currentSaveVectorsForRef(line);
        for (const QString& currentSaveVector : currentSaveVectors) {
            if (!currentSaveVector.isEmpty() && !savedCurrentVectors.contains(currentSaveVector, Qt::CaseInsensitive)) {
                savedCurrentVectors.append(currentSaveVector);
            }
        }

        // --- VioSpice Smart Block (JIT) ---
        if (type == SchematicItem::SmartSignalType || 
            typeName.contains("smartsignal", Qt::CaseInsensitive) || 
            typeName.contains("smart signal", Qt::CaseInsensitive)) {
            
            qDebug() << "[SpiceNetlistGenerator] Processing smart block:" << ref;
            // Re-read pins to get names
            QMap<QString, QString> pinsMap = componentPins.value(ref);
            qDebug() << "[SpiceNetlistGenerator] Pin map size for" << ref << ":" << pinsMap.size();
            
            // Smart Blocks typically have named pins in metadata
            // But they are exported as numeric pins on the symbol
            // We need to match schematic net names to logical pin names
            
            // For now, iterate all connected nets and identify if they are outputs or inputs
            // Based on SmartSignalItem structure: input pins first, then output pins.
            
            // Let's find the actual item to get its pin configuration
            SmartSignalItem* smartItem = nullptr;
            for (auto* item : scene->items()) {
                if (auto* si = dynamic_cast<SmartSignalItem*>(item)) {
                    if (si->reference() == ref) {
                        smartItem = si;
                        break;
                    }
                }
            }

            if (smartItem) {
                QStringList inPins = smartItem->inputPins();
                QStringList outPins = smartItem->outputPins();
                
                // Native XSPICE Integration:
                // Format: A_[REF] [in1 in2...] out1 [out2...] viospice_jit_model
                // .model viospice_jit_model viospice_jit (jit_id="REF")
                
                QStringList inNets;
                for (const QString& pin : inPins) {
                    inNets << pinsMap.value(pin, "0");
                }
                
                QStringList outNets;
                for (const QString& pin : outPins) {
                    outNets << pinsMap.value(pin, "0");
                }

                if (!outNets.isEmpty()) {
                    // We only support single output per JIT block in the first version of the code model
                    QString inVector = "[" + inNets.join(" ") + "]";
                    QString outNet = outNets.first();
                    
                    netlist += QString("A_%1 %2 %3 viospice_jit_model_%1\n").arg(ref, inVector, outNet);
                    netlist += QString(".model viospice_jit_model_%1 viospice_jit (jit_id=\"%1\")\n").arg(ref);
                    qDebug() << "[SpiceNetlistGenerator] Emitted native JIT block for" << ref;
                } else {
                    netlist += QString("* Warning: Smart Block %1 has no output pins.\n").arg(ref);
                }
            } else {
                // Fallback for missing item: default to first two pins as VSource
                QString n1 = nodes.value(0, "0");
                QString n2 = nodes.value(1, "0");
                netlist += QString("%1 %2 %3 0.0\n").arg(line, n1, n2);
            }
            continue;
        }

        // Strip unsupported voltage parasitics and emit separate elements for ngspice.
        const bool isVoltageSource = (type == SchematicItem::VoltageSourceType) ||
                                     comp.typeName.startsWith("Voltage_Source", Qt::CaseInsensitive);
        if (isVoltageSource) {
            VoltageParasitics paras = stripVoltageParasitics(value);
            value = paras.value;
            const bool hasRser = !paras.rser.isEmpty() && paras.rser != "0" && paras.rser != "0.0";
            const bool hasCpar = !paras.cpar.isEmpty() && paras.cpar != "0" && paras.cpar != "0.0";
            if ((hasRser || hasCpar) && nodes.size() >= 2) {
                QString n1 = nodes.value(0, "0");
                QString n2 = nodes.value(1, "0");
                QString srcPos = n1;
                if (hasRser) {
                    QString nInt = QString("VSR_%1").arg(ref);
                    nInt.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
                    netlist += QString("R_%1 %2 %3 %4\n").arg(ref, n1, nInt, paras.rser);
                    srcPos = nInt;
                }
                if (hasCpar) {
                    netlist += QString("C_%1 %2 %3 %4\n").arg(ref, srcPos, n2, paras.cpar);
                }
                    nodes[0] = srcPos;
                nodes[1] = n2;
            }
        }

        const bool isCurrentSource = (comp.type == SchematicItem::CurrentSourceType);
        if (isCurrentSource) {
            VoltageParasitics paras = stripVoltageParasitics(value);
            value = paras.value;
            QString rewrittenCurrentSource;
            if (LtspiceRewriter::rewriteLtspiceCurrentSourceSpecial(ref, nodes.value(0, "0"), nodes.value(1, "0"), value, projectDir,
                                                   &rewrittenCurrentSource, &directiveWarnings)) {
                netlist += rewrittenCurrentSource + "\n";
                continue;
            }
        }

        const bool isBehavioralCurrentSource = (comp.typeName.compare("Current_Source_Behavioral", Qt::CaseInsensitive) == 0) ||
                                              (comp.typeName.compare("bi", Qt::CaseInsensitive) == 0) ||
                                              (comp.typeName.compare("bi2", Qt::CaseInsensitive) == 0);
        if (isBehavioralCurrentSource) {
            QString n1 = nodes.value(0, "0");
            QString n2 = nodes.value(1, "0");

            const QString arrowDir = comp.paramExpressions.value("bi.arrow_direction").trimmed().toLower();
            const bool swapForUpArrow = (arrowDir == "up") || (comp.typeName.compare("bi2", Qt::CaseInsensitive) == 0);
            if (swapForUpArrow) {
                const QString tmp = n1;
                n1 = n2;
                n2 = tmp;
            }

            QString expr = value.trimmed();
            if (expr.isEmpty()) expr = "I=0";
            if (!expr.startsWith("I=", Qt::CaseInsensitive)) expr = "I=" + expr;

            QString bref = ref;
            if (!bref.startsWith("B", Qt::CaseInsensitive)) bref = "B" + ref;
            netlist += QString("%1 %2 %3 %4\n").arg(bref, n1, n2, expr);
            continue;
        }

        const bool isVCVS = (comp.typeName.compare("e", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("vcvs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("E", Qt::CaseInsensitive);
        const bool isVCCS = (comp.typeName.compare("g", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("vccs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("G", Qt::CaseInsensitive);

        if ((isVCVS || isVCCS)) {
            // Build nodes from pin numbers (pins map uses numeric keys "1","2","3","4")
            QStringList vcNodes;
            for (int i = 1; i <= 4; i++) {
                vcNodes.append(pins.value(QString::number(i), "0").replace(" ", "_"));
            }

            QString gain = value.trimmed();
            // Reject placeholder defaults like "E", "G", "g2", "vcvs", "vccs"
            const QString typeLower = comp.typeName.trimmed().toLower();
            const QString gainLower = gain.toLower();
            if (gain.isEmpty() || gainLower == typeLower ||
                gainLower == "e" || gainLower == "g" || gainLower == "g2" ||
                gainLower == "vcvs" || gainLower == "vccs") {
                gain = "1";
            }

            QString eref = ref;
            const QString pref = isVCVS ? "E" : "G";
            if (!eref.startsWith(pref, Qt::CaseInsensitive)) eref = pref + ref;
            netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(eref, vcNodes[0], vcNodes[1], vcNodes[2], vcNodes[3], gain);
            continue;
        }

        const bool isCCCS = (comp.typeName.compare("f", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("cccs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("F", Qt::CaseInsensitive);
        const bool isCCVS = (comp.typeName.compare("h", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("ccvs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("H", Qt::CaseInsensitive);

        if ((isCCCS || isCCVS) && nodes.size() >= 2) {
            const QString n1 = nodes.at(0);
            const QString n2 = nodes.at(1);

            // Expecting value to be "VSOURCE GAIN" or similar
            QString controlSource;
            QString gainVal = "1";
            
            QStringList parts = value.split(" ", Qt::SkipEmptyParts);
            if (parts.size() >= 1) {
                controlSource = parts[0];
                if (parts.size() >= 2) gainVal = parts[1];
            } else {
                controlSource = "V_UNKNOWN_CTRL"; 
            }

            // Apply V-prefix rule for control source
            if (!controlSource.startsWith("V", Qt::CaseInsensitive)) {
                controlSource = "V" + controlSource;
            }

            QString eref = ref;
            const QString pref = isCCCS ? "F" : "H";
            if (!eref.startsWith(pref, Qt::CaseInsensitive)) eref = pref + ref;
            netlist += QString("%1 %2 %3 %4 %5\n").arg(eref, n1, n2, controlSource, gainVal);
            continue;
        }

        const bool isLosslessTLine = (comp.typeName.compare("tline", Qt::CaseInsensitive) == 0) ||
                                     ref.startsWith("T", Qt::CaseInsensitive);
        const bool isLossyTLine = (comp.typeName.compare("ltline", Qt::CaseInsensitive) == 0) ||
                                  ref.startsWith("O", Qt::CaseInsensitive);
        if ((isLosslessTLine || isLossyTLine) && nodes.size() >= 4) {
            const QString n1 = nodes.at(0);
            const QString n2 = nodes.at(1);
            const QString n3 = nodes.at(2);
            const QString n4 = nodes.at(3);

            if (isLossyTLine) {
                QString modelName = value.trimmed();
                if (modelName.isEmpty() || modelName.compare("LTRA", Qt::CaseInsensitive) == 0) {
                    modelName = "LTRAmod";
                }
                const QString r = comp.paramExpressions.value("ltra.R").trimmed();
                const QString l = comp.paramExpressions.value("ltra.L").trimmed();
                const QString g = comp.paramExpressions.value("ltra.G").trimmed();
                const QString c = comp.paramExpressions.value("ltra.C").trimmed();
                const QString len = comp.paramExpressions.value("ltra.LEN").trimmed();

                QStringList modelTokens;
                if (!r.isEmpty()) modelTokens << QString("R=%1").arg(r);
                if (!l.isEmpty()) modelTokens << QString("L=%1").arg(l);
                if (!g.isEmpty()) modelTokens << QString("G=%1").arg(g);
                if (!c.isEmpty()) modelTokens << QString("C=%1").arg(c);
                if (!len.isEmpty()) modelTokens << QString("LEN=%1").arg(len);

                if (!modelTokens.isEmpty() && !switchModelsAdded.contains(modelName.toLower())) {
                    netlist += QString(".model %1 LTRA(%2)\n").arg(modelName, modelTokens.join(" "));
                    switchModelsAdded.insert(modelName.toLower());
                }

                QString oref = ref;
                if (!oref.startsWith("O", Qt::CaseInsensitive)) oref = "O" + ref;
                netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(oref, n1, n2, n3, n4, modelName);
            } else {
                QString z0 = "50";
                QString td = "50n";
                const QString v = value.trimmed();
                const QRegularExpression reZ0("\\bZ0\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                const QRegularExpression reTd("\\bTd\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                auto mz = reZ0.match(v);
                auto mt = reTd.match(v);
                if (mz.hasMatch()) z0 = mz.captured(1);
                if (mt.hasMatch()) td = mt.captured(1);

                QString tref = ref;
                if (!tref.startsWith("T", Qt::CaseInsensitive)) tref = "T" + ref;
                netlist += QString("%1 %2 %3 %4 %5 Z0=%6 Td=%7\n").arg(tref, n1, n2, n3, n4, z0, td);
            }
            continue;
        }

        const bool isNJF = (comp.typeName.compare("njf", Qt::CaseInsensitive) == 0);
        const bool isPJF = (comp.typeName.compare("pjf", Qt::CaseInsensitive) == 0);
        const bool isJFET = isNJF || isPJF || ref.startsWith("J", Qt::CaseInsensitive);
        if (isJFET && nodes.size() >= 3) {
            QString jref = ref;
            if (!jref.startsWith("J", Qt::CaseInsensitive)) jref = "J" + ref;

            QString model = value.trimmed();
            if (model.isEmpty() || model.compare("njf", Qt::CaseInsensitive) == 0 || model.compare("pjf", Qt::CaseInsensitive) == 0) {
                model = isPJF ? "2N5460" : "2N3819";
            }

            if (!switchModelsAdded.contains(model.toLower()) && !ModelLibraryManager::instance().findModel(model)) {
                const QString typeToken = isPJF ? "PJF" : "NJF";
                const QString vto = isPJF ? "2" : "-2";
                netlist += QString(".model %1 %2(Beta=1m Vto=%3 Lambda=0.02 Rd=1 Rs=1 Cgs=2p Cgd=1p Is=1e-14)\n")
                    .arg(model, typeToken, vto);
                switchModelsAdded.insert(model.toLower());
            }

            const QString d = nodes.at(0);
            const QString g = nodes.at(1);
            const QString s = nodes.at(2);
            netlist += QString("%1 %2 %3 %4 %5\n").arg(jref, d, g, s, model);
            continue;
        }

        const bool isMesfet = (comp.typeName.compare("mesfet", Qt::CaseInsensitive) == 0) ||
                               ref.startsWith("Z", Qt::CaseInsensitive);
        if (isMesfet && nodes.size() >= 3) {
            QString zref = ref;
            if (!zref.startsWith("Z", Qt::CaseInsensitive)) zref = "Z" + ref;

            QString model = value.trimmed();
            if (model.isEmpty()) model = "NMF";

            if (!switchModelsAdded.contains(model.toLower()) && !ModelLibraryManager::instance().findModel(model)) {
                const bool pchannel = model.compare("PMF", Qt::CaseInsensitive) == 0;
                const QString mtype = pchannel ? "PMF" : "NMF";
                const QString vto = pchannel ? "2.1" : "-2.1";
                netlist += QString(".model %1 %2(Vto=%3 Beta=0.05 Lambda=0.02 Alpha=3 B=0.5 Rd=1 Rs=1 Cgs=1p Cgd=0.2p)\n")
                    .arg(model, mtype, vto);
                switchModelsAdded.insert(model.toLower());
            }

            const QString d = nodes.at(0);
            const QString g = nodes.at(1);
            const QString s = nodes.at(2);
            netlist += QString("%1 %2 %3 %4 %5\n").arg(zref, d, g, s, model);
            continue;
        }

        const bool isVoltageControlledSwitch = (comp.typeName.compare("Voltage Controlled Switch", Qt::CaseInsensitive) == 0);
        if (isVoltageControlledSwitch) {
            const QString n1 = nodes.value(0, "0");
            const QString n2 = nodes.value(1, "0");
            const QString ctrlp = nodes.value(2, "0");
            const QString ctrln = nodes.value(3, "0");

            QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
            if (modelName.isEmpty()) modelName = QString("SW_%1").arg(ref);

            QString ron = comp.paramExpressions.value("switch.ron").trimmed();
            if (ron.isEmpty()) ron = "0.1";
            QString roff = comp.paramExpressions.value("switch.roff").trimmed();
            if (roff.isEmpty()) roff = "1Meg";
            QString vt = comp.paramExpressions.value("switch.vt").trimmed();
            if (vt.isEmpty()) vt = "0.5";
            QString vh = comp.paramExpressions.value("switch.vh").trimmed();
            if (vh.isEmpty()) vh = "0.1";

            if (!switchModelsAdded.contains(modelName)) {
                netlist += QString(".model %1 SW(Ron=%2 Roff=%3 Vt=%4 Vh=%5)\n")
                               .arg(modelName, ron, roff, vt, vh);
                switchModelsAdded.insert(modelName);
            }

            QString switchRef = ref;
            if (!switchRef.startsWith("S", Qt::CaseInsensitive)) switchRef = "S" + ref;
            netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(switchRef, n1, n2, ctrlp, ctrln, modelName);
            continue;
        }

        const bool isPotentiometer = (comp.typeName.compare("Potentiometer", Qt::CaseInsensitive) == 0) ||
                                     ref.startsWith("RPOT", Qt::CaseInsensitive);
        if (isPotentiometer && nodes.size() >= 3) {
            const QString r0 = nodes.at(0);
            const QString wiper = nodes.at(1);
            const QString r1 = nodes.at(2);

            QString res = value.trimmed();
            if (res.isEmpty()) res = "10k";

            QString pos = comp.paramExpressions.value("pot.position").trimmed();
            if (pos.isEmpty()) pos = "0.5";
            QString isLog = comp.paramExpressions.value("pot.log").trimmed().toUpper();
            if (isLog.isEmpty()) isLog = "FALSE";
            QString logMult = comp.paramExpressions.value("pot.log_multiplier").trimmed();
            if (logMult.isEmpty()) logMult = "1.0";

            QString modelName = QString("pot_mod_%1").arg(ref);
            netlist += QString(".model %1 potentiometer(r=%2 position=%3 log=%4 log_multiplier=%5)\n")
                           .arg(modelName, res, pos, isLog, logMult);

            QString potRef = ref;
            if (!potRef.startsWith("A", Qt::CaseInsensitive)) potRef = "A_" + ref;
            netlist += QString("%1 [%2 %3 %4] %5\n").arg(potRef, r0, wiper, r1, modelName);
            continue;
        }

        const bool isSmartSignal = (type == SchematicItem::SmartSignalType) ||
                                   comp.typeName.compare("SmartSignalBlock", Qt::CaseInsensitive) == 0;
        const bool isCSW = (comp.typeName.compare("csw", Qt::CaseInsensitive) == 0) || baseRef.startsWith("W", Qt::CaseInsensitive);
        const bool isSwitch = !isSmartSignal &&
                              ((comp.typeName.compare("Switch", Qt::CaseInsensitive) == 0) ||
                               (comp.typeName.compare("sw", Qt::CaseInsensitive) == 0) ||
                               baseRef.startsWith("SW", Qt::CaseInsensitive) ||
                               baseRef.startsWith("S", Qt::CaseInsensitive) ||
                               isCSW);
        if (isSwitch) {
            // If the symbol provides control pins, treat it as a voltage-controlled switch.
            if (nodes.size() >= 4 && !isCSW) {
                const QString n1 = nodes.at(0);
                const QString n2 = nodes.at(1);
                const QString ctrlp = nodes.at(2);
                const QString ctrln = nodes.at(3);

                QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
                if (modelName.isEmpty()) modelName = QString("SW_%1").arg(ref);

                QString ron = comp.paramExpressions.value("switch.ron").trimmed();
                if (ron.isEmpty()) ron = "0.1";
                QString roff = comp.paramExpressions.value("switch.roff").trimmed();
                if (roff.isEmpty()) roff = "1Meg";
                QString vt = comp.paramExpressions.value("switch.vt").trimmed();
                if (vt.isEmpty()) vt = "0.5";
                QString vh = comp.paramExpressions.value("switch.vh").trimmed();
                if (vh.isEmpty()) vh = "0.1";

                if (!switchModelsAdded.contains(modelName)) {
                    netlist += QString(".model %1 SW(Ron=%2 Roff=%3 Vt=%4 Vh=%5)\n")
                                   .arg(modelName, ron, roff, vt, vh);
                    switchModelsAdded.insert(modelName);
                }

                QString switchRef = ref;
                if (!switchRef.startsWith("S", Qt::CaseInsensitive)) switchRef = "S" + ref;
                netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(switchRef, n1, n2, ctrlp, ctrln, modelName);
                continue;
            }

            const QString n1 = nodes.value(0, "0");
            const QString n2 = nodes.value(1, "0");
            if (isCSW) {
                QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
                QString controlSource = comp.paramExpressions.value("switch.control_source").trimmed();

                if (modelName.isEmpty() && !value.isEmpty()) {
                    QStringList parts = value.split(" ", Qt::SkipEmptyParts);
                    if (parts.size() >= 2 && parts[0].startsWith("V", Qt::CaseInsensitive)) {
                        if (controlSource.isEmpty()) controlSource = parts[0];
                        modelName = parts[1];
                    } else if (parts.size() >= 1) {
                        modelName = parts[0];
                    }
                }

                if (modelName.isEmpty()) modelName = QString("CSW_%1").arg(ref);

                QString ron = comp.paramExpressions.value("switch.ron").trimmed();
                if (ron.isEmpty()) ron = comp.paramExpressions.value("csw.ron").trimmed();
                if (ron.isEmpty()) ron = "1";
                
                QString roff = comp.paramExpressions.value("switch.roff").trimmed();
                if (roff.isEmpty()) roff = comp.paramExpressions.value("csw.roff").trimmed();
                if (roff.isEmpty()) roff = "1Meg";
                
                QString it = comp.paramExpressions.value("switch.it").trimmed();
                if (it.isEmpty()) it = comp.paramExpressions.value("csw.it").trimmed();
                if (it.isEmpty()) it = "1m";
                
                QString ih = comp.paramExpressions.value("switch.ih").trimmed();
                if (ih.isEmpty()) ih = comp.paramExpressions.value("csw.ih").trimmed();
                if (ih.isEmpty()) ih = "0.2m";

                if (!switchModelsAdded.contains(modelName.toLower())) {
                    netlist += QString(".model %1 CSW(Ron=%2 Roff=%3 It=%4 Ih=%5)\n")
                                   .arg(modelName, ron, roff, it, ih);
                    switchModelsAdded.insert(modelName.toLower());
                }

                if (controlSource.isEmpty()) {
                    controlSource = "V_UNKNOWN_CTRL"; // Placeholder if user didn't specify
                } else if (!controlSource.startsWith("V", Qt::CaseInsensitive)) {
                    controlSource = "V" + controlSource;
                }
                QString switchRef = ref;
                if (!switchRef.startsWith("W", Qt::CaseInsensitive)) switchRef = "W" + ref;
                netlist += QString("%1 %2 %3 %4 %5\n").arg(switchRef, n1, n2, controlSource, modelName);
                continue;
            }

            const QString useModelExpr = comp.paramExpressions.value("switch.use_model").trimmed();
            const bool useModel = (useModelExpr == "1" || useModelExpr.compare("true", Qt::CaseInsensitive) == 0);

            if (useModel) {
                QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
                if (modelName.isEmpty()) modelName = QString("SW_%1").arg(ref);

                QString ron = comp.paramExpressions.value("switch.ron").trimmed();
                if (ron.isEmpty()) ron = "0.1";
                QString roff = comp.paramExpressions.value("switch.roff").trimmed();
                if (roff.isEmpty()) roff = "1Meg";
                QString vt = comp.paramExpressions.value("switch.vt").trimmed();
                if (vt.isEmpty()) vt = "0.5";
                QString vh = comp.paramExpressions.value("switch.vh").trimmed();
                if (vh.isEmpty()) vh = "0.1";

                if (!switchModelsAdded.contains(modelName)) {
                    netlist += QString(".model %1 SW(Ron=%2 Roff=%3 Vt=%4 Vh=%5)\n")
                                   .arg(modelName, ron, roff, vt, vh);
                    switchModelsAdded.insert(modelName);
                }

                QString switchRef = ref;
                if (!switchRef.startsWith("S", Qt::CaseInsensitive)) switchRef = "S" + ref;
                QString ctlNode = QString("SWCTL_%1").arg(ref);

                const QString stateExpr = comp.paramExpressions.value("switch.state").trimmed().toLower();
                const bool isOpen = (stateExpr.isEmpty() ? true : (stateExpr == "open"));

                bool okVt = false;
                bool okVh = false;
                const double vtNum = vt.toDouble(&okVt);
                const double vhNum = vh.toDouble(&okVh);
                const double vhAbs = okVh ? std::abs(vhNum) : 0.1;
                const double vtBase = okVt ? vtNum : 0.5;
                const double high = vtBase + vhAbs + 0.1;
                const double low = vtBase - vhAbs - 0.1;
                const double controlV = isOpen ? low : high;

                QString vref = QString("VSW_%1").arg(ref);
                if (!vref.startsWith("V", Qt::CaseInsensitive)) vref = "V" + vref;

                netlist += QString("%1 %2 %3 %4 0 %5\n").arg(switchRef, n1, n2, ctlNode, modelName);
                netlist += QString("%1 %2 0 DC %3\n").arg(vref, ctlNode, QString::number(controlV, 'g', 6));
                continue;
            }

            QString switchRef = ref;
            if (!switchRef.startsWith("R", Qt::CaseInsensitive)) switchRef = "R" + ref;
            QString switchValue = value.isEmpty() ? "1e12" : value;
            netlist += QString("%1 %2 %3 %4\n").arg(switchRef, n1, n2, switchValue);
            continue;
        }

        QStringList emittedNodes = nodes;
        if (!isADevice && !digitalDrivenNets.isEmpty()) {
            for (int nodeIdx = 0; nodeIdx < emittedNodes.size(); ++nodeIdx) {
                const QString node = emittedNodes.at(nodeIdx);
                if (node.startsWith("[") && node.endsWith("]")) continue;
                if (!digitalDrivenNets.contains(node)) continue;

                const QString bridgedNode = QString("__MM_DAC_%1_%2")
                                               .arg(XSpiceBlockTranslator::sanitizeMixedModeToken(ref))
                                               .arg(nodeIdx + 1);
                const QString pinLabel = QString::number(nodeIdx + 1);
                netlist += XSpiceBlockTranslator::mixedModeDacBridgeLine(ref, pinLabel, node, bridgedNode) + "\n";
                runtimeWarnings.append(QString("Inserted dac_bridge on %1 pin %2 so XSPICE digital net %3 can drive an analog/SPICE node.").arg(ref, pinLabel, node));
                emittedNodes[nodeIdx] = bridgedNode;
            }
        }
        if (line.startsWith("X", Qt::CaseInsensitive)) {
            const SimSubcircuit* subForCount = activeSub ? activeSub : nullptr;
            // Prefer the actual value token (model/subckt name) if already known.
            if (!subForCount && !value.trimmed().isEmpty()) {
                subForCount = ModelLibraryManager::instance().findSubcircuit(value.trimmed());
            }
            // Fallback to symbol model name.
            if (!subForCount && sym && !sym->modelName().trimmed().isEmpty()) {
                subForCount = ModelLibraryManager::instance().findSubcircuit(sym->modelName().trimmed());
            }

            if (subForCount) {
                const int subPins = static_cast<int>(subForCount->pinNames.size());
                if (subPins > 0) {
                    if (emittedNodes.size() > subPins) {
                        netlist += QString("* Warning: Trimming extra pins for %1 (%2 -> %3) to match subckt '%4'\n")
                                       .arg(ref)
                                       .arg(emittedNodes.size())
                                       .arg(subPins)
                                       .arg(QString::fromStdString(subForCount->name));
                        emittedNodes = emittedNodes.mid(0, subPins);
                    } else if (emittedNodes.size() < subPins) {
                        netlist += QString("* Warning: Padding missing pins for %1 (%2 -> %3) to match subckt '%4'\n")
                                       .arg(ref)
                                       .arg(emittedNodes.size())
                                       .arg(subPins)
                                       .arg(QString::fromStdString(subForCount->name));
                        while (emittedNodes.size() < subPins) emittedNodes.append("0");
                    }
                }
            }
        }

        if (line.startsWith("Q", Qt::CaseInsensitive) && emittedNodes.size() == 4) {
            const QString sub = emittedNodes.at(3).trimmed();
            if (sub.isEmpty() || sub == "0") {
                emittedNodes[3] = emittedNodes.at(2);
            }
        }

        for (const QString& node : emittedNodes) {
            line += " " + node;
        }

        // ngspice MOSFET requires 4 nodes: D G S B. For 3-pin symbols, tie body to source.
        if (line.startsWith("M", Qt::CaseInsensitive) && emittedNodes.size() == 3) {
            line += " " + emittedNodes.at(2);
        }

        auto isGenericBuiltInDiodePlaceholder = [&](const QString& candidate) -> bool {
            const QString normalized = candidate.trimmed().toLower();
            if (normalized.isEmpty()) return false;
            if (ModelLibraryManager::instance().findModel(candidate) ||
                ModelLibraryManager::instance().findSubcircuit(candidate)) {
                return false;
            }
            return normalized == "d" ||
                   normalized == "diode" ||
                   normalized == "diode_silicon" ||
                   normalized == "silicon";
        };

        if (line.startsWith("D") && isGenericBuiltInDiodePlaceholder(value)) {
            value.clear();
        }

        // Add value
        if (value.isEmpty()) {
            if (isADevice) {
                // LTspice digital symbols and generic logic symbols frequently store
                // aliases like AND, gate_and, DFF, BUF. XSPICE requires a .model
                // instance whose type is the real code model, e.g. d_and or d_dff.
                value = comp.paramExpressions.value("ltspice.SpiceModel").trimmed();
                if (value.isEmpty()) value = comp.paramExpressions.value("ltspice.MODEL").trimmed();
                if (value.isEmpty()) value = comp.paramExpressions.value("ltspice.Model").trimmed();
                if (value.isEmpty() && sym) {
                    if (!sym->spiceModelName().trimmed().isEmpty()) value = sym->spiceModelName().trimmed();
                    else if (!sym->modelName().trimmed().isEmpty()) value = sym->modelName().trimmed();
                }
                if (value.isEmpty()) {
                    const QString tl = comp.typeName.trimmed().toLower();
                    if (tl.contains("xnor")) value = "XNOR";
                    else if (tl.contains("xor")) value = "XOR";
                    else if (tl.contains("nand")) value = "NAND";
                    else if (tl.contains("nor")) value = "NOR";
                    else if (tl.contains("and")) value = "AND";
                    else if (tl.contains("or")) value = "OR";
                    else if (tl.contains("inv") || tl.contains("not")) value = "INV";
                    else if (tl.contains("buf")) value = "BUF";
                    else if (tl.contains("jk")) value = "JKFF";
                    else if (tl.contains("sr") && tl.contains("latch")) value = "SRLATCH";
                    else if (tl.contains("sr")) value = "SRFF";
                    else if (tl.contains("dlatch") || tl.contains("d_latch")) value = "DLATCH";
                    else if (tl.contains("dff") || tl.contains("flip")) value = "DFF";
                    else value = "AND";
                }
            } else
            if (line.startsWith("D")) {
                // Generate default .model for diodes with no model specified
                QString defaultModel = QString("D_DEFAULT_%1").arg(ref);
                netlist += QString(".model %1 D(Is=2.52n N=1.752 Rs=0.568 Vj=0.7 Cjo=4p M=0.4 tt=20n)\n")
                    .arg(defaultModel);
                value = defaultModel;
            } else if (line.startsWith("M", Qt::CaseInsensitive)) {
                const QString mosTypeExpr = comp.paramExpressions.value("mos.type").trimmed();
                const bool pmosAlias = mosTypeExpr.compare("PMOS", Qt::CaseInsensitive) == 0 ||
                                       comp.typeName.compare("Transistor_PMOS", Qt::CaseInsensitive) == 0 ||
                                       comp.typeName.compare("pmos", Qt::CaseInsensitive) == 0 ||
                                       comp.typeName.compare("pmos4", Qt::CaseInsensitive) == 0 ||
                                       ref.startsWith("MP", Qt::CaseInsensitive);
                value = pmosAlias ? "BS250" : "2N7000";
            } else if (line.startsWith("Q")) {
                const QString bjtTypeExpr = comp.paramExpressions.value("bjt.type").trimmed();
                const bool pnpAlias = bjtTypeExpr.compare("PNP", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("transistor_pnp", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("pnp", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("pnp2", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("pnp4", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("lpnp", Qt::CaseInsensitive) == 0 ||
                                      ref.startsWith("QP", Qt::CaseInsensitive);
                value = pnpAlias ? "2N3906" : "2N2222";
            } else {
                value = "1k"; // Default for R/C/L
            }
        } else if (line.startsWith("M", Qt::CaseInsensitive) && (value.compare("NMOS", Qt::CaseInsensitive) == 0 || value.compare("PMOS", Qt::CaseInsensitive) == 0)) {
            value = (value.compare("PMOS", Qt::CaseInsensitive) == 0) ? "BS250" : "2N7000";
        } else if (line.startsWith("Q") && (value.compare("NPN", Qt::CaseInsensitive) == 0 || value.compare("PNP", Qt::CaseInsensitive) == 0)) {
            value = (value.compare("PNP", Qt::CaseInsensitive) == 0) ? "2N3906" : "2N2222";
        }

        if (line.startsWith("M", Qt::CaseInsensitive)) {
            const ComponentExtractor::SpiceTokenSplit split = ComponentExtractor::splitLeadingSpiceToken(value);
            if (!split.head.isEmpty()) {
                value = split.head;
                instanceSuffix = split.tail;
            }
        }

        if (line.startsWith("M", Qt::CaseInsensitive) && !switchModelsAdded.contains(value.toLower()) && !ModelLibraryManager::instance().findModel(value)) {
            const QString mosTypeExpr = comp.paramExpressions.value("mos.type").trimmed();
            const bool pmosModel = mosTypeExpr.compare("PMOS", Qt::CaseInsensitive) == 0 ||
                                   value.compare("BS250", Qt::CaseInsensitive) == 0 ||
                                   comp.typeName.compare("Transistor_PMOS", Qt::CaseInsensitive) == 0 ||
                                   comp.typeName.compare("pmos", Qt::CaseInsensitive) == 0 ||
                                   comp.typeName.compare("pmos4", Qt::CaseInsensitive) == 0 ||
                                   ref.startsWith("MP", Qt::CaseInsensitive);

            // Check for model level in params
            const QString mosLevel = comp.paramExpressions.value("mos.level").trimmed();
            const QString mosLevelUpper = mosLevel.toUpper();

            if (mosLevelUpper == "BSIM4") {
                const QString vth0 = pmosModel ? "-0.35" : "0.35";
                netlist += QString(".model %1 %2(LEVEL=14 Vth0=%3 Toxp=1e-10)\n")
                    .arg(value, pmosModel ? "PMOS" : "NMOS", vth0);
            } else if (mosLevelUpper == "BSIMSOI") {
                const QString vth0 = pmosModel ? "-0.35" : "0.35";
                netlist += QString(".model %1 %2(LEVEL=10 Vth0=%3 Toxp=1e-10)\n")
                    .arg(value, pmosModel ? "PMOS" : "NMOS", vth0);
            } else if (mosLevelUpper == "BSIM3") {
                const QString vto = pmosModel ? "-0.4" : "0.4";
                netlist += QString(".model %1 %2(LEVEL=8 Vth0=%3 U0=0.04 Vsat=1.1e5)\n")
                    .arg(value, pmosModel ? "PMOS" : "NMOS", vto);
            } else if (mosLevelUpper == "BSIM3SOI") {
                const QString vto = pmosModel ? "-0.35" : "0.35";
                netlist += QString(".model %1 %2(LEVEL=55 Vth0=%3)\n")
                    .arg(value, pmosModel ? "PMOS" : "NMOS", vto);
            } else if (mosLevelUpper == "HISIM2") {
                const QString vth = pmosModel ? "-0.35" : "0.35";
                netlist += QString(".model %1 %2(LEVEL=68 Vth=%3 Mu0=0.045)\n")
                    .arg(value, pmosModel ? "PMOS" : "NMOS", vth);
            } else if (mosLevelUpper == "HISIM_HV") {
                const QString vth = pmosModel ? "-30" : "30";
                netlist += QString(".model %1 %2(LEVEL=73 Vth=%3)\n")
                    .arg(value, pmosModel ? "PMOS" : "NMOS", vth);
            } else if (mosLevelUpper == "VDMOS" || mosLevelUpper == "VDMOSN") {
                netlist += QString(".model %1 VDMOS(Vto=%2 Rd=1 Rs=1 Kp=5)\n")
                    .arg(value, pmosModel ? "-2" : "2");
            } else if (mosLevelUpper == "VDMOSP") {
                netlist += QString(".model %1 VDMOSP(Vto=%2 Rd=1 Rs=1 Kp=5)\n")
                    .arg(value, pmosModel ? "-2" : "2");
            } else {
                // Default: Level 1-3 style
                const QString mosType = pmosModel ? "PMOS" : "NMOS";
                const QString vto = pmosModel ? "-2" : "2";

                if (mosLevelUpper == "MOS2") {
                    netlist += QString(".model %1 %2(LEVEL=2 Vto=%3 Kp=100u Lambda=0.02 Rd=1 Rs=1 Cgso=50p Cgdo=50p)\n")
                        .arg(value, mosType, vto);
                } else if (mosLevelUpper == "MOS3") {
                    netlist += QString(".model %1 %2(LEVEL=3 Vto=%3 Kp=100u Lambda=0.02 Rd=1 Rs=1 Cgso=50p Cgdo=50p)\n")
                        .arg(value, mosType, vto);
                } else {
                    netlist += QString(".model %1 %2(Vto=%3 Kp=100u Lambda=0.02 Rd=1 Rs=1 Cgso=50p Cgdo=50p)\n")
                        .arg(value, mosType, vto);
                }
            }
            switchModelsAdded.insert(value.toLower());
        }

        if (line.startsWith("Q") && !switchModelsAdded.contains(value.toLower()) && !ModelLibraryManager::instance().findModel(value)) {
            const QString bjtTypeExpr = comp.paramExpressions.value("bjt.type").trimmed();
            const bool pnpModel = bjtTypeExpr.compare("PNP", Qt::CaseInsensitive) == 0 ||
                                  value.compare("2N3906", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("transistor_pnp", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("pnp", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("pnp2", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("pnp4", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("lpnp", Qt::CaseInsensitive) == 0 ||
                                  ref.startsWith("QP", Qt::CaseInsensitive);
            const QString bjtType = pnpModel ? "PNP" : "NPN";
            netlist += QString(".model %1 %2(Is=1e-14 Bf=100 Vaf=100 Cje=8p Cjc=3p Tf=400p Tr=50n)\n")
                .arg(value, bjtType);
            switchModelsAdded.insert(value.toLower());
        }

        if (isADevice && !isNativeLogicADevice) {
            const QString codeModel = ConnectivityEvaluator::normalizeXspiceModelAlias(value, comp.typeName);
            if (codeModel.isEmpty()) {
                runtimeWarnings.append(QString("Unknown XSPICE gate model '%1' on %2; defaulted to d_and.").arg(value, ref));
                value = QString("__XSPICE_%1").arg(XSpiceBlockTranslator::sanitizeMixedModeToken(ref));
                const QString modelLine = defaultXspiceModelLine(ref, "d_and");
                if (!switchModelsAdded.contains(value.toLower())) {
                    netlist += modelLine + "\n";
                    switchModelsAdded.insert(value.toLower());
                }
            } else {
                const QString modelName = QString("__XSPICE_%1").arg(XSpiceBlockTranslator::sanitizeMixedModeToken(ref));
                const QString modelLine = defaultXspiceModelLine(ref, codeModel);
                value = modelName;
                if (!switchModelsAdded.contains(modelName.toLower())) {
                    netlist += modelLine + "\n";
                    switchModelsAdded.insert(modelName.toLower());
                }
            }
        } else if (isNativeLogicADevice) {
            const QString nativeKeyword = nativeLogicKeywordForCodeModel(normalizedLogicCodeModel);
            if (!nativeKeyword.isEmpty()) {
                // Guardrail:
                // On the native path we emit the LT-style keyword (`DLATCH`,
                // `DFF`, `AND`, ...) instead of explicit `__MM_DAC/__MM_ADC`
                // bridge wiring. That preserves direct schematic probeability on
                // external pins when the backend is VioMATRIXC. Do not mix this
                // with bridge aliases unless you intentionally want internal
                // implementation nodes to leak into waveform selection.
                value = nativeKeyword;
                // Collect and emit the model card for the native keyword if not already added
                if (!switchModelsAdded.contains(value.toLower())) {
                    const QString modelLine = QString(".model %1 %2(rise_delay=1n fall_delay=1n)")
                                                .arg(nativeKeyword, normalizedLogicCodeModel);
                    netlist += modelLine + "\n";
                    switchModelsAdded.insert(value.toLower());
                }
            }
        }

        if ((line.startsWith("L", Qt::CaseInsensitive) || line.startsWith("C", Qt::CaseInsensitive)) &&
            emittedNodes.size() >= 2) {
            const PassiveCompanionParams passiveParams = parsePassiveCompanionParams(value);
            const bool hasCompanions = !passiveParams.rser.isEmpty() || !passiveParams.rpar.isEmpty() || !passiveParams.cpar.isEmpty();
            if (hasCompanions && !passiveParams.baseValue.isEmpty()) {
                const QString node1 = emittedNodes.at(0);
                const QString node2 = emittedNodes.at(1);
                const QString deviceNode1 = passiveParams.rser.isEmpty() ? node1 : QString("%1__rser").arg(ref);
                const QString elementRef = line.section(QRegularExpression("\\s+"), 0, 0).trimmed();

                if (!passiveParams.rser.isEmpty()) {
                    netlist += QString("R__RSER_%1 %2 %3 %4\n").arg(ref, node1, deviceNode1, passiveParams.rser);
                }
                if (!passiveParams.rpar.isEmpty()) {
                    netlist += QString("R__RPAR_%1 %2 %3 %4\n").arg(ref, deviceNode1, node2, passiveParams.rpar);
                }
                if (!passiveParams.cpar.isEmpty()) {
                    netlist += QString("C__CPAR_%1 %2 %3 %4\n").arg(ref, deviceNode1, node2, passiveParams.cpar);
                }

                QString mainLine = QString("%1 %2 %3 %4").arg(elementRef, deviceNode1, node2, passiveParams.baseValue);
                if (line.startsWith("L", Qt::CaseInsensitive) && !passiveParams.ic.isEmpty()) {
                    mainLine += " ic=" + passiveParams.ic;
                }
                mainLine += "\n";
                netlist += mainLine;

                runtimeWarnings.append(QString("Expanded inline parasitics on %1 into explicit companion elements for ngspice compatibility.").arg(ref));
                continue;
            }
        }

        line += " " + value;
        if (!instanceSuffix.isEmpty()) {
            line += " " + instanceSuffix;
        }

        // VioSpice S-Parameter Mode: Add portnum and z0
        if (params.type == SParameter && (ref.startsWith("V", Qt::CaseInsensitive) || ref.startsWith("I", Qt::CaseInsensitive))) {
            QString refOnly = ref.trimmed().toUpper();
            QString p1Source = params.rfPort1Source.trimmed().toUpper();
            QString z0 = params.rfZ0.isEmpty() ? "50" : params.rfZ0;
            
            if (refOnly == p1Source || (p1Source.isEmpty() && refOnly.startsWith("V"))) {
                if (!line.contains("portnum", Qt::CaseInsensitive)) {
                    line += QString(" portnum=1 z0=%1 AC 1").arg(z0);
                }
            }
        } else if (params.type == AC && (ref.startsWith("V", Qt::CaseInsensitive) || ref.startsWith("I", Qt::CaseInsensitive))) {
            QString refOnly = ref.trimmed().toUpper();
            QString targetSource = params.rfPort1Source.trimmed().toUpper();
            if (refOnly == targetSource || (targetSource.isEmpty() && refOnly.startsWith("V"))) {
                if (!line.contains(" AC ", Qt::CaseInsensitive)) {
                    line += " AC 1";
                }
            }
        }

        if (!value.endsWith("\n")) line += "\n";
        
        netlist += line;
    }

    NetlistFormatter::format(params,
                             powerNetVoltages,
                             userDrivenRailNets,
                             savedCurrentVectors,
                             directiveWarnings,
                             runtimeWarnings,
                             hasUserElementCards,
                             hasNetDirective,
                             hasExplicitAnalysisCard,
                             hasExplicitSaveDirective,
                             netlist);
    return { netlist, componentPins };
}

QString SpiceNetlistGenerator::buildCommand(const SimulationParams& params) {
    switch (params.type) {
        case Transient:
        {
            if (!params.transientMaxStep.trimmed().isEmpty()) {
                const QString tstart = (params.start.trimmed().isEmpty() || params.start.trimmed() == "0")
                    ? QString("0") : params.start.trimmed();
                QString command = QString(".tran %1 %2 %3 %4").arg(params.step, params.stop, tstart, params.transientMaxStep);
                if (params.transientSteady) {
                    command += " steady";
                }
                return command;
            }
            QString command = QString(".tran %1 %2").arg(params.step, params.stop);
            if (!params.start.trimmed().isEmpty() && params.start.trimmed() != "0") {
                command += QString(" %1").arg(params.start.trimmed());
            }
            if (params.transientSteady) {
                command += " steady";
            }
            return command;
        }
        case DC:
            return QString(".dc %1 %2 %3 %4").arg(params.dcSource, params.dcStart, params.dcStop, params.dcStep);
        case AC: {
            auto safeNumber = [](const QString& text, double fallback) {
                double parsed = 0.0;
                if (SimValueParser::parseSpiceNumber(text, parsed) && parsed > 0.0) {
                    return text.trimmed();
                }
                return QString::number(fallback, 'g', 12);
            };
            const QString pts = safeNumber(params.step, 10.0);
            const QString start = safeNumber(params.start, 10.0);
            const QString stop = safeNumber(params.stop, 1e6);
            QString type = "dec";
            if (params.acSweepType == SimAcSweepType::Octave) type = "oct";
            else if (params.acSweepType == SimAcSweepType::Linear) type = "lin";
            return QString(".ac %1 %2 %3 %4").arg(type, pts, start, stop);
        }
        case OP:
            return ".op";
        case Noise: {
            const QString output = params.noiseOutput.isEmpty() ? "V(out)" : params.noiseOutput;
            const QString source = params.noiseSource.isEmpty() ? "V1" : params.noiseSource;
            const QString pts = params.step.isEmpty() ? "10" : params.step;
            const QString fstart = params.start.isEmpty() ? "1" : params.start;
            const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
            return QString(".noise %1 %2 %3 %4 %5").arg(output, source, pts, fstart, fstop);
        }
        case Fourier: {
            const QString freq = params.fourFreq.isEmpty() ? "1k" : params.fourFreq;
            QStringList outputs = params.fourOutputs;
            if (outputs.isEmpty()) outputs << "V(out)";
            return QString(".four %1 %2").arg(freq, outputs.join(" "));
        }
        case TF: {
            const QString output = params.tfOutput.isEmpty() ? "V(out)" : params.tfOutput;
            const QString source = params.tfSource.isEmpty() ? "V1" : params.tfSource;
            return QString(".tf %1 %2").arg(output, source);
        }
        case Disto: {
            const QString pts = params.step.isEmpty() ? "10" : params.step;
            const QString fstart = params.start.isEmpty() ? "1" : params.start;
            const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
            if (!params.distoF2OverF1.isEmpty()) {
                return QString(".disto %1 %2 %3 %4").arg(pts, fstart, fstop, params.distoF2OverF1);
            }
            return QString(".disto %1 %2 %3").arg(pts, fstart, fstop);
        }
        case Meas:
            return params.measRaw.isEmpty() ? ".meas" : params.measRaw;
        case Step:
            return params.stepRaw.isEmpty() ? ".step" : params.stepRaw;
        case Sens: {
            const QString output = params.sensOutput.isEmpty() ? "V(out)" : params.sensOutput;
            return QString(".sens %1").arg(output);
        }
        case FFT:
            return ".fft";
        case SParameter: {
            const QString pts = params.step.isEmpty() ? "10" : params.step;
            const QString start = params.start.isEmpty() ? "1Meg" : params.start;
            const QString stop = params.stop.isEmpty() ? "1G" : params.stop;
            return QString(".sp dec %1 %2 %3").arg(pts, start, stop);
        }
    }
    return ".op";
}





QString SpiceNetlistGenerator::formatValue(double value) {
    if (value == 0) return "0";
    if (value < 1e-9) return QString::number(value * 1e12) + "p";
    if (value < 1e-6) return QString::number(value * 1e9) + "n";
    if (value < 1e-3) return QString::number(value * 1e06) + "u";
    if (value < 1) return QString::number(value * 1e3) + "m";
    return QString::number(value);
}

QString SpiceNetlistGenerator::generateCompatibilityLayer(const QString& rawNetlist) {
    if (rawNetlist.isEmpty()) return QString();

    QString processed = rawNetlist;
    processed = processed.trimmed();

    // Split by newline and handle potential different newline formats
    QStringList lines = processed.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    QStringList outLines;
    QStringList warnings;

    for (QString line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('*') || trimmed.startsWith(';') || trimmed.startsWith('#') || trimmed.startsWith('+')) {
            outLines << line; // Keep continuation lines as-is, don't try to rewrite them
            continue;
        }

        // If the line already uses native VioMATRIXC pwlfile, don't mess with it
        if (trimmed.contains("pwlfile=", Qt::CaseInsensitive)) {
            outLines << line;
            continue;
        }

        line = LtspiceRewriter::rewriteLtspiceDirectiveLine(line, &warnings);

        outLines << line;
    }

    return outLines.join('\n');
}
