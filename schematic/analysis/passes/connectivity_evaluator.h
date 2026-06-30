/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CONNECTIVITY_EVALUATOR_H
#define CONNECTIVITY_EVALUATOR_H

#include <QString>
#include <QStringList>
#include <QSet>
#include <QMap>
#include "eco_types.h"
#include "../../../simulator/mixedmode/NetlistManager.h" // for PinDirection and NodeType

struct ConnectivityResult {
    QMap<QString, QMap<QString, QString>> componentPins;
    QMap<QString, QString> powerNetMapping;
    QMap<QString, QString> powerNetVoltages;
    QSet<QString> digitalDrivenNets;
    QStringList runtimeWarnings;
};

class ConnectivityEvaluator {
public:
    static ConnectivityResult evaluate(const ECOPackage& pkg, const QSet<QString>& userDrivenRailNets);
    static QString pickPowerNetName(const QMap<QString, QString>& pins, const QString& fallbackValue);
    static QString inferPowerVoltage(const QString& netName, const QString& valueText);
};

#endif // CONNECTIVITY_EVALUATOR_H
