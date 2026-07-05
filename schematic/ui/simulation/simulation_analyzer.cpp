/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "simulation_analyzer.h"
#include "../../items/schematic_item.h"
#include "../../analysis/net_manager.h"

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QSet>
#include <QLineF>
#include <QPointF>
#include <QDebug>
#include <algorithm>
#include <cmath>

QString SimulationAnalyzer::canonicalWaveformNetName(const QString& rawName) {
    const QString trimmed = rawName.trimmed();
    if (trimmed.startsWith("V(", Qt::CaseInsensitive) && trimmed.endsWith(')')) {
        return trimmed.mid(2, trimmed.size() - 3).trimmed();
    }
    return trimmed;
}

QStringList SimulationAnalyzer::waveformNetAliases(const QString& netName) {
    const QString trimmed = netName.trimmed();
    if (trimmed.isEmpty()) return {};

    QStringList aliases{trimmed};
    
    if (trimmed.startsWith("I(", Qt::CaseInsensitive) && trimmed.endsWith(")")) {
        QString comp = trimmed.mid(2, trimmed.size() - 3).trimmed();
        aliases << QString("@%1[i]").arg(comp) << QString("@%1[I]").arg(comp);
        aliases << QString("%1#branch").arg(comp) << QString("%1#BRANCH").arg(comp);
        
        aliases << QString("I(%1(C))").arg(comp) << QString("I(%1(B))").arg(comp) << QString("I(%1(E))").arg(comp);
        aliases << QString("I(%1(D))").arg(comp) << QString("I(%1(G))").arg(comp) << QString("I(%1(S))").arg(comp);
        aliases << QString("I(%1:C)").arg(comp) << QString("I(%1:B)").arg(comp) << QString("I(%1:E)").arg(comp);
        aliases << QString("I(%1:D)").arg(comp) << QString("I(%1:G)").arg(comp) << QString("I(%1:S)").arg(comp);
        
        aliases << QString("@%1[ic]").arg(comp) << QString("@%1[ib]").arg(comp) << QString("@%1[ie]").arg(comp);
        aliases << QString("@%1[id]").arg(comp) << QString("@%1[ig]").arg(comp) << QString("@%1[is]").arg(comp);

        aliases << QString("I(V(%1))").arg(comp);
        aliases << comp;
    } else if (trimmed.startsWith("P(", Qt::CaseInsensitive) && trimmed.endsWith(")")) {
        QString comp = trimmed.mid(2, trimmed.size() - 3).trimmed();
        aliases << QString("@%1[p]").arg(comp) << QString("@%1[P]").arg(comp);
    } else {
        aliases << QString("V(%1)").arg(trimmed);
        if (trimmed.startsWith("V(", Qt::CaseInsensitive) && trimmed.endsWith(")")) {
            aliases << trimmed.mid(2, trimmed.size() - 3).trimmed();
        }
    }

    const QString upper = trimmed.toUpper();
    if (upper == "GND" || trimmed == "0") {
        aliases << "0" << "GND" << "V(0)" << "V(GND)";
    }
    aliases.removeDuplicates();
    return aliases;
}

const SimWaveform* SimulationAnalyzer::findWaveByNetAliases(const std::vector<SimWaveform>& waveforms, const QString& netName) {
    const QStringList aliases = waveformNetAliases(netName);
    for (const auto& wave : waveforms) {
        const QString waveName = QString::fromStdString(wave.name).trimmed();
        const QString canonicalWaveName = canonicalWaveformNetName(waveName);
        for (const QString& alias : aliases) {
            if (waveName.compare(alias, Qt::CaseInsensitive) == 0 ||
                canonicalWaveName.compare(alias, Qt::CaseInsensitive) == 0) {
                return &wave;
            }
        }
    }
    return nullptr;
}

bool SimulationAnalyzer::signalMatches(const QString& itemText, const QString& signalName) {
    const QStringList aliases = waveformNetAliases(signalName);
    for (const QString& alias : aliases) {
        if (itemText.compare(alias, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    QString textNoSuffix = itemText;
    int bracketIdx = textNoSuffix.indexOf(" [");
    if (bracketIdx > 0) {
        textNoSuffix = textNoSuffix.left(bracketIdx).trimmed();
        for (const QString& alias : aliases) {
            if (textNoSuffix.compare(alias, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
    }
    return false;
}

QStringList SimulationAnalyzer::connectedNetsForItem(SchematicItem* item, QGraphicsScene* scene, NetManager* netManager, bool updateNets) {
    QStringList nets;
    if (!item || !netManager) return nets;
    if (updateNets && scene) netManager->updateNets(scene);

    QSet<QString> seen;
    const qreal pinTolerance = 2.0;

    const QList<QPointF> pins = item->connectionPoints();
    for (int i = 0; i < pins.size(); ++i) {
        const QPointF pinScene = item->mapToScene(pins[i]);
        QString net = netManager->findNetAtPoint(pinScene).trimmed();

        if (net.isEmpty()) {
            net = item->pinNet(i).trimmed();
        }
        if (net.isEmpty()) continue;

        const QList<NetConnection> conns = netManager->getConnections(net);
        bool pinBelongsToItem = false;
        for (const auto& conn : conns) {
            if (conn.item != item) continue;
            if (QLineF(conn.connectionPoint, pinScene).length() <= pinTolerance) {
                pinBelongsToItem = true;
                break;
            }
        }
        if (!pinBelongsToItem && !net.isEmpty()) {
            pinBelongsToItem = true;
        }

        if (!pinBelongsToItem) continue;

        const QString canonicalNet = net.toUpper();
        if (seen.contains(canonicalNet)) continue;
        seen.insert(canonicalNet);
        nets.append(net);
    }
    return nets;
}

bool SimulationAnalyzer::buildDerivedPowerWaveform(
    const QString& signalName, 
    QVector<double>& time, 
    QVector<double>& values,
    QGraphicsScene* scene,
    NetManager* netManager,
    const std::vector<SimWaveform>& waveforms) 
{
    if (!signalName.startsWith("P(", Qt::CaseInsensitive) || !signalName.endsWith(")")) return false;
    if (!scene) { qWarning() << "buildDerivedPowerWaveform: no scene"; return false; }

    const QString ref = signalName.mid(2, signalName.length() - 3).trimmed();
    if (ref.isEmpty()) return false;

    SchematicItem* targetItem = nullptr;
    for (QGraphicsItem* gi : scene->items()) {
        auto* item = dynamic_cast<SchematicItem*>(gi);
        if (!item) continue;
        if (item->reference().compare(ref, Qt::CaseInsensitive) == 0) {
            targetItem = item;
            break;
        }
    }
    if (!targetItem) { qWarning() << "buildDerivedPowerWaveform: no item with ref" << ref; return false; }

    const QStringList nets = connectedNetsForItem(targetItem, scene, netManager, false);
    if (nets.size() < 2) { qWarning() << "buildDerivedPowerWaveform:" << ref << "has" << nets.size() << "nets, need >= 2"; return false; }

    const SimWaveform* currentWave = nullptr;
    const QString currentName = QString("I(%1)").arg(ref);
    const QString posName = QString("V(%1)").arg(nets.value(0));
    const QString negName = QString("V(%1)").arg(nets.value(1));

    for (const auto& w : waveforms) {
        const QString wName = QString::fromStdString(w.name);
        if (!currentWave && wName.compare(currentName, Qt::CaseInsensitive) == 0) currentWave = &w;
    }
    const SimWaveform* posWave = findWaveByNetAliases(waveforms, nets.value(0));
    const SimWaveform* negWave = findWaveByNetAliases(waveforms, nets.value(1));
    if (!currentWave || !posWave || !negWave) {
        qWarning() << "buildDerivedPowerWaveform:" << ref
                   << "current=" << (currentWave ? "OK" : "MISSING")
                   << "pos=" << (posWave ? "OK" : "MISSING") << "(" << posName << ")"
                   << "neg=" << (negWave ? "OK" : "MISSING") << "(" << negName << ")";
        return false;
    }

    const size_t count = std::min({currentWave->xData.size(), currentWave->yData.size(), posWave->yData.size(), negWave->yData.size()});
    if (count == 0) return false;

    time.reserve(static_cast<int>(count));
    values.reserve(static_cast<int>(count));
    for (size_t i = 0; i < count; ++i) {
        time.append(currentWave->xData[i]);
        values.append((posWave->yData[i] - negWave->yData[i]) * currentWave->yData[i]);
    }
    return true;
}

void SimulationAnalyzer::appendDerivedPowerWaveforms(SimResults& results, QGraphicsScene* scene, NetManager* netManager) {
    if (!scene || !netManager) return;
    netManager->updateNets(scene);

    QSet<QString> existing;
    for (const auto& w : results.waveforms) {
        existing.insert(QString::fromStdString(w.name).toUpper());
    }

    std::vector<SimWaveform> powerWaves;
    for (QGraphicsItem* gi : scene->items()) {
        auto* item = dynamic_cast<SchematicItem*>(gi);
        if (!item) continue;

        const QString ref = item->reference().trimmed();
        if (ref.isEmpty()) continue;

        const QStringList nets = connectedNetsForItem(item, scene, netManager, false);
        if (nets.size() < 2) continue;

        const QString baseCurrentName = QString("I(%1)").arg(ref);
        const QString basePowerName = QString("P(%1)").arg(ref);

        for (const auto& w : results.waveforms) {
            QString wName = QString::fromStdString(w.name);
            if (!signalMatches(wName, baseCurrentName)) continue;

            QString stepSuffix;
            int bracketIdx = wName.indexOf(" [");
            if (bracketIdx > 0) {
                stepSuffix = wName.mid(bracketIdx);
            }

            const QString powerName = basePowerName + stepSuffix;
            if (existing.contains(powerName.toUpper())) continue;

            const SimWaveform* posWave = nullptr;
            const SimWaveform* negWave = nullptr;

            for (const auto& vw : results.waveforms) {
                QString vwName = QString::fromStdString(vw.name);
                
                if (signalMatches(vwName, nets[0])) {
                    int vBracketIdx = vwName.indexOf(" [");
                    QString vSuffix = (vBracketIdx > 0) ? vwName.mid(vBracketIdx) : "";
                    if (vSuffix == stepSuffix) posWave = &vw;
                }
                
                if (signalMatches(vwName, nets[1])) {
                    int vBracketIdx = vwName.indexOf(" [");
                    QString vSuffix = (vBracketIdx > 0) ? vwName.mid(vBracketIdx) : "";
                    if (vSuffix == stepSuffix) negWave = &vw;
                }
            }

            if (!posWave || !negWave) continue;

            const size_t count = std::min({w.xData.size(), w.yData.size(), posWave->yData.size(), negWave->yData.size()});
            if (count == 0) continue;

            SimWaveform pWave;
            pWave.name = powerName.toStdString();
            pWave.xData.reserve(count);
            pWave.yData.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                pWave.xData.push_back(w.xData[i]);
                pWave.yData.push_back((posWave->yData[i] - negWave->yData[i]) * w.yData[i]);
            }
            powerWaves.push_back(std::move(pWave));
        }
    }
    
    if (!powerWaves.empty()) {
        results.waveforms.insert(results.waveforms.end(), 
                                 std::make_move_iterator(powerWaves.begin()), 
                                 std::make_move_iterator(powerWaves.end()));
    }
}

void SimulationAnalyzer::appendEfficiencySummary(SimResults& results, QGraphicsScene* scene, NetManager* netManager) {
    if (!scene || !netManager || results.analysisType != SimAnalysisType::Transient) return;

    auto findWave = [&](const QString& name) -> const SimWaveform* {
        for (const auto& wave : results.waveforms) {
            if (QString::fromStdString(wave.name).compare(name, Qt::CaseInsensitive) == 0) {
                return &wave;
            }
        }
        return nullptr;
    };

    auto averageTail = [](const SimWaveform* wave) -> double {
        if (!wave || wave->yData.empty()) return 0.0;
        const size_t count = wave->yData.size();
        const size_t begin = (count > 10) ? (count * 9) / 10 : 0;
        double sum = 0.0;
        size_t used = 0;
        for (size_t i = begin; i < count; ++i) {
            sum += wave->yData[i];
            ++used;
        }
        return used ? (sum / static_cast<double>(used)) : 0.0;
    };

    QStringList voltageSources;
    QStringList loadRefs;
    for (QGraphicsItem* gi : scene->items()) {
        auto* item = dynamic_cast<SchematicItem*>(gi);
        if (!item) continue;
        const QString ref = item->reference().trimmed();
        if (ref.isEmpty()) continue;
        if (ref.startsWith('V', Qt::CaseInsensitive)) {
            if (findWave(QString("P(%1)").arg(ref))) voltageSources.append(ref);
        } else if (ref.compare("RLOAD", Qt::CaseInsensitive) == 0 ||
                   ref.startsWith('I', Qt::CaseInsensitive)) {
            if (findWave(QString("P(%1)").arg(ref))) loadRefs.append(ref);
        }
    }

    if (voltageSources.size() != 1 || loadRefs.size() != 1) return;

    const QString inputRef = voltageSources.first();
    const QString outputRef = loadRefs.first();
    const SimWaveform* inputWave = findWave(QString("P(%1)").arg(inputRef));
    const SimWaveform* outputWave = findWave(QString("P(%1)").arg(outputRef));
    if (!inputWave || !outputWave) return;

    const double inputAvgRaw = averageTail(inputWave);
    const double outputAvgRaw = averageTail(outputWave);
    const double inputPower = std::abs(inputAvgRaw);
    const double outputPower = std::abs(outputAvgRaw);
    if (inputPower <= 0.0 || outputPower <= 0.0) return;

    const double efficiencyPct = (outputPower / inputPower) * 100.0;
    results.measurements["eff_input_power_w"] = inputPower;
    results.measurements["eff_output_power_w"] = outputPower;
    results.measurements["efficiency_pct"] = efficiencyPct;

    results.measurementMetadata["eff_input_power_w"] = {"Input Power", "W"};
    results.measurementMetadata["eff_output_power_w"] = {"Output Power", "W"};
    results.measurementMetadata["efficiency_pct"] = {"Efficiency", "%"};
    results.diagnostics.push_back(
        QString("Efficiency summary: input=%1 W, output=%2 W, eta=%3 % using %4 as source and %5 as load.")
            .arg(inputPower, 0, 'g', 6)
            .arg(outputPower, 0, 'g', 6)
            .arg(efficiencyPct, 0, 'g', 5)
            .arg(inputRef)
            .arg(outputRef)
            .toStdString()
    );
}
