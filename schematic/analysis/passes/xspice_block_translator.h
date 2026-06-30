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

class XSpiceBlockTranslator {
public:
    static bool translate(const ECOComponent& comp,
                         const QMap<QString, QString>& pins,
                         const QString& projectDir,
                         const QSet<QString>& digitalDrivenNets,
                         QString& netlist,
                         QStringList& runtimeWarnings,
                         const QList<ECONet>& nets = QList<ECONet>());
};

#endif // XSPICE_BLOCK_TRANSLATOR_H
