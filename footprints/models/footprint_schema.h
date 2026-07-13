/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FOOTPRINT_SCHEMA_H
#define FOOTPRINT_SCHEMA_H

namespace Flux {
namespace Model {
namespace Schema {
    constexpr const char* X = "x";
    constexpr const char* Y = "y";
    constexpr const char* Width = "width";
    constexpr const char* Height = "height";
    
    constexpr const char* X1 = "x1";
    constexpr const char* Y1 = "y1";
    constexpr const char* X2 = "x2";
    constexpr const char* Y2 = "y2";
    
    constexpr const char* CX = "cx";
    constexpr const char* CY = "cy";
    constexpr const char* Radius = "radius";
    
    constexpr const char* StartAngle = "startAngle";
    constexpr const char* SpanAngle = "spanAngle";
    
    constexpr const char* Points = "points";
    
    // Pad specific keys
    constexpr const char* Shape = "shape";
    constexpr const char* Number = "number";
    constexpr const char* PadType = "pad_type";
    constexpr const char* DrillSize = "drill_size";
    constexpr const char* Rotation = "rotation";
    constexpr const char* CornerRadius = "corner_radius";
    constexpr const char* TrapezoidDeltaX = "trapezoid_delta_x";
    
    // Pad design rules
    constexpr const char* NetClearance = "net_clearance";
    constexpr const char* NetClearanceOverrideEnabled = "net_clearance_override_enabled";
    constexpr const char* ThermalReliefEnabled = "thermal_relief_enabled";
    constexpr const char* ThermalSpokeWidth = "thermal_spoke_width";
    constexpr const char* ThermalReliefGap = "thermal_relief_gap";
    constexpr const char* ThermalSpokeCount = "thermal_spoke_count";
    constexpr const char* ThermalSpokeAngleDeg = "thermal_spoke_angle_deg";
    constexpr const char* JumperGroup = "jumper_group";
    constexpr const char* NetTieGroup = "net_tie_group";
    constexpr const char* SolderMaskExpansion = "solder_mask_expansion";
    constexpr const char* PasteMaskExpansion = "paste_mask_expansion";
    constexpr const char* Plated = "plated";
    
    // Text specific keys
    constexpr const char* Text = "text";
    
    // Common attributes
    constexpr const char* LineWidth = "lineWidth";
    constexpr const char* Filled = "filled";
}
}
}

#endif // FOOTPRINT_SCHEMA_H
