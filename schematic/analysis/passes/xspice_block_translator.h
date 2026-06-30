/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef XSPICE_BLOCK_TRANSLATOR_H
#define XSPICE_BLOCK_TRANSLATOR_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QList>
#include "eco_types.h"

namespace Flux {
namespace Model {
class SymbolDefinition;
}
}

class XSpiceBlockTranslator {
public:
    static bool translate(const ECOComponent& comp,
                         const QMap<QString, QString>& pins,
                         const QString& projectDir,
                         const QSet<QString>& digitalDrivenNets,
                         QString& netlist,
                         QStringList& runtimeWarnings,
                         const QList<ECONet>& nets = QList<ECONet>());

    static bool naturalPinLessThan(const QString& s1, const QString& s2);
    static QString sanitizeMixedModeToken(const QString& raw);
    static QString mixedModeAdcBridgeLine(const QString& ref, const QString& pinName, const QString& analogNet, const QString& digitalNet);
    static QString mixedModeDacBridgeLine(const QString& ref, const QString& pinName, const QString& digitalNet, const QString& analogNet);
    static QString normalizeXspiceGateModelAlias(const QString& rawToken, const QString& typeName = QString());
    static QStringList buildXspiceNodeTokensForPins(const QMap<QString, QString>& pins,
                                                     const Flux::Model::SymbolDefinition* symbol = nullptr,
                                                     bool collapseScalarInputsToVector = false);
};

#endif // XSPICE_BLOCK_TRANSLATOR_H
