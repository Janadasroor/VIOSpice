/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NETLIST_FORMATTER_H
#define NETLIST_FORMATTER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include "../spice_netlist_generator.h"

class NetlistFormatter {
public:
    static void format(const SpiceNetlistGenerator::SimulationParams& params,
                       const QMap<QString, QString>& powerNetVoltages,
                       const QSet<QString>& userDrivenRailNets,
                       const QStringList& savedCurrentVectors,
                       const QStringList& directiveWarnings,
                       const QStringList& runtimeWarnings,
                       bool hasUserElementCards,
                       bool hasNetDirective,
                       bool hasExplicitAnalysisCard,
                       bool hasExplicitSaveDirective,
                       QString& netlist);
};

#endif // NETLIST_FORMATTER_H
