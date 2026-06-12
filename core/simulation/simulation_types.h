/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SIMULATION_TYPES_H
#define SIMULATION_TYPES_H

#include <QStringList>
#include <QMap>

namespace Flux {

struct FluxScriptTarget {
    QStringList outputVoltageSources;
    QMap<QString, QString> pinToNetMap;
};

} // namespace Flux

#endif // SIMULATION_TYPES_H
