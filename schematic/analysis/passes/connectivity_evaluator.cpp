/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "connectivity_evaluator.h"
#include "../spice_netlist_generator.h"
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
        const bool isADevice = SpiceNetlistGenerator::isXspiceLogicComponent(rawLogicToken, comp.typeName, ref);
        const QString codeModel = SpiceNetlistGenerator::normalizeXspiceModelAlias(rawLogicToken, comp.typeName);
        if (!isADevice || (comp.typeName != "XspiceBlock" && SpiceNetlistGenerator::usesNativeLogicADevice(codeModel))) continue;

        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(comp.typeName);
        const QMap<QString, QString> pins = res.componentPins.value(ref);
        for (auto it = pins.constBegin(); it != pins.constEnd(); ++it) {
            const QString heuristicPinName = SpiceNetlistGenerator::pinNameForHeuristics(sym, it.key());
            bool hasDomainMetadata = false;
            const NodeType domain = SpiceNetlistGenerator::pinDomainFromMetadata(sym, it.key(), &hasDomainMetadata);
            bool hasDirectionMetadata = false;
            const NetlistManager::PinDirection direction = SpiceNetlistGenerator::pinDirectionFromMetadata(sym, it.key(), &hasDirectionMetadata);

            if (hasDomainMetadata && domain == NodeType::DIGITAL_EVENT &&
                hasDirectionMetadata &&
                (direction == NetlistManager::PinDirection::OUTPUT || direction == NetlistManager::PinDirection::BIDIRECTIONAL)) {
                maybeDigitalNet(it.value());
                continue;
            }

            if (SpiceNetlistGenerator::isLikelyLogicOutputPinName(heuristicPinName)) {
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
