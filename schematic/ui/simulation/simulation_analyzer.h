/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SIMULATION_ANALYZER_H
#define SIMULATION_ANALYZER_H

#include "../../../simulator/core/sim_results.h"
#include <QStringList>
#include <QVector>

class QGraphicsScene;
class NetManager;
class SchematicItem;

class SimulationAnalyzer {
public:
    static QString canonicalWaveformNetName(const QString& rawName);
    static QStringList waveformNetAliases(const QString& netName);
    static const SimWaveform* findWaveByNetAliases(const std::vector<SimWaveform>& waveforms, const QString& netName);
    static bool signalMatches(const QString& itemText, const QString& signalName);

    static QStringList connectedNetsForItem(SchematicItem* item, QGraphicsScene* scene, NetManager* netManager, bool updateNets = true);
    
    static bool buildDerivedPowerWaveform(
        const QString& signalName, 
        QVector<double>& time, 
        QVector<double>& values,
        QGraphicsScene* scene,
        NetManager* netManager,
        const std::vector<SimWaveform>& waveforms);

    static void appendDerivedPowerWaveforms(SimResults& results, QGraphicsScene* scene, NetManager* netManager);
    static void appendEfficiencySummary(SimResults& results, QGraphicsScene* scene, NetManager* netManager);
};

#endif // SIMULATION_ANALYZER_H
