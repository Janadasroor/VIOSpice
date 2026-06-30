/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "connectivity_evaluator.h"
#include "../../../core/simulation/simulation_manager.h"
#include "../../../symbols/symbol_library.h"
#include "../../../symbols/models/symbol_definition.h"
#include "../../../simulator/core/sim_value_parser.h"
#include "../../items/schematic_item.h"
#include <QDebug>
#include <QRegularExpression>

using Flux::Model::SymbolDefinition;

ConnectivityResult ConnectivityEvaluator::evaluate(const ECOPackage& pkg, const QSet<QString>& userDrivenRailNets) {
    ConnectivityResult res;

    // 1. Gather the pin mappings
    for (const auto& net : pkg.nets) {
        QString netName = net.name;
        if (netName.toUpper() == "GND" || netName == "0") netName = "0";
        for (const auto& pin : net.pins) {
            res.componentPins[pin.componentRef][pin.pinName] = netName;
        }
    }

    // 2. Maps power nets and voltages
    QSet<QString> emittedPowerSymbols; // Track power symbols to avoid processing duplicates
    for (const auto& comp : pkg.components) {
        if (comp.type == SchematicItem::PowerType) {
            QString ref = comp.reference;
            QMap<QString, QString> pins = res.componentPins.value(ref);
            QString netName = pickPowerNetName(pins, comp.value);
            
            // For power symbols, we only skip if it's the SAME reference AND SAME net.
            QString emitKey = ref + ":" + netName;
            if (emittedPowerSymbols.contains(emitKey)) continue;
            emittedPowerSymbols.insert(emitKey);
            
            if (netName.isEmpty()) continue;
            
            QString v = inferPowerVoltage(netName, comp.value);
            double val = 0.0;
            SimValueParser::parseSpiceNumber(v, val);
            
            const QString uNet = netName.trimmed().toUpper();
            const QString uVal = comp.value.trimmed().toUpper();

            if (val > 0) {
                if (!res.powerNetMapping.contains("VCC") || uNet.contains("VCC"))
                    res.powerNetMapping["VCC"] = netName;
            } else if (val < 0) {
                if (!res.powerNetMapping.contains("VEE") || uNet.contains("VEE"))
                    res.powerNetMapping["VEE"] = netName;
            }
            
            // Explicit name matching
            if (uNet == "VCC" || uNet == "VDD" || uNet == "V+" || uVal == "VCC" || uVal == "V+") 
                res.powerNetMapping["VCC"] = netName;
            else if (uNet == "VEE" || uNet == "VSS" || uNet == "V-" || uVal == "VEE" || uVal == "V-") 
                res.powerNetMapping["VEE"] = netName;
            else if (uNet == "GND" || uNet == "0" || uVal == "GND" || uVal == "0") 
                res.powerNetMapping["GND"] = "0";

            // Populate powerNetVoltages and handle warnings if userDrivenRailNets contains the net
            if (netName.toUpper() != "GND" && netName != "0") {
                res.powerNetVoltages[netName] = v;
                if (userDrivenRailNets.contains(netName.toUpper())) {
                    res.runtimeWarnings.append(QString("Manual directive source already drives schematic power rail %1; skipped auto-generated rail source.").arg(netName));
                }
            }
        }
    }

    // 3. Detects digital-driven nets
    auto maybeDigitalNet = [&](const QString& netName) {
        const QString net = netName.trimmed().replace(' ', '_');
        if (!net.isEmpty() && net != "0") res.digitalDrivenNets.insert(net);
    };
    for (const auto& comp : pkg.components) {
        if (comp.excludeFromSim) continue;
        const QString ref = comp.reference;
        const QString rawLogicToken = comp.spiceModel.trimmed().isEmpty() ? comp.value.trimmed() : comp.spiceModel.trimmed();
        const bool isADevice = ConnectivityEvaluator::isXspiceLogicComponent(rawLogicToken, comp.typeName, ref);
        const QString codeModel = ConnectivityEvaluator::normalizeXspiceModelAlias(rawLogicToken, comp.typeName);
        if (!isADevice || (comp.typeName != "XspiceBlock" && ConnectivityEvaluator::usesNativeLogicADevice(codeModel))) continue;

        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(comp.typeName);
        const QMap<QString, QString> pins = res.componentPins.value(ref);
        for (auto it = pins.constBegin(); it != pins.constEnd(); ++it) {
            const QString heuristicPinName = ConnectivityEvaluator::pinNameForHeuristics(sym, it.key());
            bool hasDomainMetadata = false;
            const NodeType domain = ConnectivityEvaluator::pinDomainFromMetadata(sym, it.key(), &hasDomainMetadata);
            bool hasDirectionMetadata = false;
            const NetlistManager::PinDirection direction = ConnectivityEvaluator::pinDirectionFromMetadata(sym, it.key(), &hasDirectionMetadata);

            if (hasDomainMetadata && domain == NodeType::DIGITAL_EVENT &&
                hasDirectionMetadata &&
                (direction == NetlistManager::PinDirection::OUTPUT || direction == NetlistManager::PinDirection::BIDIRECTIONAL)) {
                maybeDigitalNet(it.value());
                continue;
            }

            if (ConnectivityEvaluator::isLikelyLogicOutputPinName(heuristicPinName)) {
                maybeDigitalNet(it.value());
            }
        }
    }

    return res;
}

QString ConnectivityEvaluator::pickPowerNetName(const QMap<QString, QString>& pins, const QString& fallbackValue) {
    QString netName = pins.value("1").trimmed();
    if (!netName.isEmpty()) return netName;

    for (auto it = pins.constBegin(); it != pins.constEnd(); ++it) {
        const QString candidate = it.value().trimmed();
        if (!candidate.isEmpty()) return candidate;
    }
    return fallbackValue.trimmed();
}

QString ConnectivityEvaluator::inferPowerVoltage(const QString& netName, const QString& valueText) {
    // Allow explicit numeric overrides such as "12", "3.3", "5V", "12v".
    const QString raw = valueText.trimmed();
    if (!raw.isEmpty()) {
        static const QRegularExpression numWithOptionalV(
            QStringLiteral("^([+-]?\\d*\\.?\\d+)\\s*[vV]?$"));
        const QRegularExpressionMatch m = numWithOptionalV.match(raw);
        if (m.hasMatch()) return m.captured(1);
    }

    const QString upperNet = netName.toUpper();
    const QString upperVal = valueText.toUpper();
    
    // Explicit negative indicators
    if (upperNet.contains("VEE") || upperNet.contains("VSS") || upperNet.contains("V-") ||
        upperVal.contains("VEE") || upperVal.contains("VSS") || upperVal.contains("V-")) {
        return "-5"; // Fallback to a negative value to trigger VEE mapping
    }

    if (upperNet.contains("12V")) return "12";
    if (upperNet.contains("9V")) return "9";
    if (upperNet.contains("5V")) return "5";
    if (upperNet.contains("3.3V") || upperNet.contains("3V3")) return "3.3";
    if (upperNet.contains("1.8V") || upperNet.contains("1V8")) return "1.8";
    if (upperNet.contains("VBAT") || upperNet.contains("BAT")) return "3.7";
    return "5";
}

bool ConnectivityEvaluator::isLikelyLogicOutputPinName(const QString& rawPinName) {
    QString pin = rawPinName.trimmed().toLower();
    static const QRegularExpression re("(\\[[0-9]+\\]|<[0-9]+>|[0-9]+)$");
    pin.remove(re);
    if (pin.isEmpty()) return false;
    return pin.contains("out") || pin == "q" || pin == "qn" || pin == "qbar" ||
           pin == "y" || pin == "z" || pin == "f" || pin == "sum" || pin == "carry";
}

QString ConnectivityEvaluator::pinNameForHeuristics(const Flux::Model::SymbolDefinition* sym, const QString& pinIdentifier) {
    if (!sym) return pinIdentifier;
    const auto* pin = sym->pinPrimitive(pinIdentifier);
    if (!pin) return pinIdentifier;
    const QString name = pin->data.value("name").toString().trimmed();
    return name.isEmpty() ? pinIdentifier : name;
}

NetlistManager::PinDirection ConnectivityEvaluator::pinDirectionFromMetadata(const Flux::Model::SymbolDefinition* sym, const QString& pinName, bool* hasExplicitMetadata) {
    if (hasExplicitMetadata) *hasExplicitMetadata = false;
    if (!sym) return NetlistManager::PinDirection::INPUT;

    const QString direction = sym->pinSignalDirection(pinName);
    if (direction == "input") {
        if (hasExplicitMetadata) *hasExplicitMetadata = true;
        return NetlistManager::PinDirection::INPUT;
    }
    if (direction == "output") {
        if (hasExplicitMetadata) *hasExplicitMetadata = true;
        return NetlistManager::PinDirection::OUTPUT;
    }
    if (direction == "bidirectional") {
        if (hasExplicitMetadata) *hasExplicitMetadata = true;
        return NetlistManager::PinDirection::BIDIRECTIONAL;
    }

    return NetlistManager::PinDirection::INPUT;
}

NodeType ConnectivityEvaluator::pinDomainFromMetadata(const Flux::Model::SymbolDefinition* sym, const QString& pinName, bool* hasExplicitMetadata) {
    if (hasExplicitMetadata) *hasExplicitMetadata = false;
    if (!sym) return NodeType::ANALOG;

    const QString domain = sym->pinSignalDomain(pinName);
    if (domain == "digital" || domain == "digital_event" || domain == "event") {
        if (hasExplicitMetadata) *hasExplicitMetadata = true;
        return NodeType::DIGITAL_EVENT;
    }
    if (domain == "analog") {
        if (hasExplicitMetadata) *hasExplicitMetadata = true;
        return NodeType::ANALOG;
    }

    return NodeType::ANALOG;
}

bool ConnectivityEvaluator::isXspiceLogicComponent(const QString& rawToken, const QString& typeName, const QString& reference) {
    if (reference.startsWith("A", Qt::CaseInsensitive)) {
        return true;
    }
    return !normalizeXspiceModelAlias(rawToken, typeName).isEmpty();
}

QString ConnectivityEvaluator::normalizeXspiceModelAlias(const QString& rawToken, const QString& typeName) {
    const QString token = rawToken.trimmed().toLower();
    const QString type = typeName.trimmed().toLower();

    auto matches = [&](std::initializer_list<const char*> vals) {
        for (const char* v : vals) {
            const QString q = QString::fromLatin1(v);
            if (token == q || type == q) return true;
        }
        return false;
    };

    if (matches({"d_and", "and", "gate_and", "and_gate"})) return "d_and";
    if (matches({"d_nand", "nand", "gate_nand", "nand_gate"})) return "d_nand";
    if (matches({"d_or", "or", "gate_or", "or_gate"})) return "d_or";
    if (matches({"d_nor", "nor", "gate_nor", "nor_gate"})) return "d_nor";
    if (matches({"d_xor", "xor", "gate_xor", "xor_gate"})) return "d_xor";
    if (matches({"d_xnor", "xnor", "gate_xnor", "xnor_gate"})) return "d_xnor";
    if (matches({"d_buffer", "buffer", "buf", "gate_buf", "gate_buffer"})) return "d_buffer";
    if (matches({"d_inverter", "inverter", "inv", "not", "gate_not", "not_gate"})) return "d_inverter";
    if (matches({"d_dff", "dff", "flipflop", "flip_flop", "d_flipflop", "d_flip_flop", "dff_gate", "dflop"})) return "d_dff";
    if (matches({"d_jkff", "jkff", "jk_flipflop", "jk_flip_flop", "jkflop"})) return "d_jkff";
    if (matches({"d_tff", "tff", "toggle_flipflop", "toggle_flip_flop", "toggleflop", "t_flipflop", "t_flip_flop"})) return "d_tff";
    if (matches({"d_srff", "srff", "sr_flipflop", "sr_flip_flop", "set_reset_flipflop", "set_reset_flip_flop"})) return "d_srff";
    if (matches({"d_dlatch", "dlatch", "d_latch", "d_type_latch"})) return "d_dlatch";
    if (matches({"d_srlatch", "srlatch", "sr_latch", "set_reset_latch"})) return "d_srlatch";
    if (matches({"d_tristate", "tristate", "tri_state"})) return "d_tristate";
    if (matches({"d_ram", "ram", "memory", "digital_ram"})) return "d_ram";

    if (token.startsWith("d_")) return token;
    return QString();
}

namespace {
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

bool ConnectivityEvaluator::usesNativeLogicADevice(const QString& codeModel) {
    if (nativeLogicKeywordForCodeModel(codeModel).isEmpty()) {
        return false;
    }
    return SimulationManager::instance().isNativeSmartSignalMode();
}
