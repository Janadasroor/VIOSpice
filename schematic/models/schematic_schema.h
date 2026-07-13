/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCHEMATIC_SCHEMA_H
#define SCHEMATIC_SCHEMA_H

#include <QString>

namespace Flux {
namespace SchematicSchema {

// Common properties
inline const QString Id = "id";
inline const QString Type = "type";
inline const QString X = "x";
inline const QString Y = "y";
inline const QString Rotation = "rotation";
inline const QString MirroredX = "mirroredX";
inline const QString MirroredY = "mirroredY";
inline const QString Unit = "unit";
inline const QString Name = "name";
inline const QString Value = "value";
inline const QString Reference = "reference";

// Component / Model properties
inline const QString Footprint = "footprint";
inline const QString Pins = "pins";
inline const QString Length = "length";
inline const QString Angle = "angle";
inline const QString NetName = "netName";

// Wire properties
inline const QString WireType = "wireType";
inline const QString Points = "points";

} // namespace SchematicSchema
} // namespace Flux

#endif // SCHEMATIC_SCHEMA_H
