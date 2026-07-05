/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COMPONENT_FORMATTER_H
#define COMPONENT_FORMATTER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include "eco_types.h"
#include "../spice_netlist_generator.h"

class QGraphicsScene;

class ComponentFormatter {
public:
    static void format(const ECOComponent& comp,
                       QGraphicsScene* scene,
                       const QString& projectDir,
                       const SpiceNetlistGenerator::SimulationParams& params,
                       const QMap<QString, QMap<QString, QString>>& componentPins,
                       const QMap<QString, QString>& powerNetMapping,
                       const QMap<QString, QString>& powerNetVoltages,
                       const QSet<QString>& userElementRefs,
                       const QSet<QString>& digitalDrivenNets,
                       const QList<ECONet>& nets,
                       QSet<QString>& emittedRefs,
                       QSet<QString>& switchModelsAdded,
                       QStringList& runtimeWarnings,
                       QStringList& directiveWarnings,
                       QStringList& savedCurrentVectors,
                       QString& netlist);
};

#endif // COMPONENT_FORMATTER_H
