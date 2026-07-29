/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCB_STACKUP_MANAGER_H
#define PCB_STACKUP_MANAGER_H

#include <QJsonObject>
#include <QList>
#include <QString>

/**
 * @brief Interactive Layer Stackup & Impedance Calculator (KiCad 8 Parity)
 * 
 * Manages substrate materials, copper weights, dielectric layers, and computes
 * single-ended microstrip ($Z_0$) and differential pair ($Z_{diff}$) characteristic impedance.
 */
class PCBStackupManager {
public:
    struct SubstrateLayer {
        QString name;
        QString type; // "Copper", "Core", "Prepreg", "Soldermask"
        double thicknessMm = 0.20; // Dielectric/copper thickness in mm
        double epsilonR = 4.5;    // Relative permittivity (FR-4)
        double copperWeightOz = 1.0; // Copper foil weight (oz/ft^2)
    };

    struct StackupInfo {
        int copperLayers = 2;
        double totalThicknessMm = 1.6;
        QList<SubstrateLayer> layers;
    };

    static StackupInfo createStandardStackup(int copperLayerCount = 2, double boardThicknessMm = 1.6);
    
    /// Calculate Microstrip Characteristic Impedance Z0 (Ohms)
    static double calculateMicrostripZ0(double traceWidthMm, double dielectricThicknessMm, double copperThicknessMm = 0.035, double epsilonR = 4.5);

    /// Calculate Differential Pair Impedance Zdiff (Ohms)
    static double calculateDiffImpedance(double traceWidthMm, double spacingMm, double dielectricThicknessMm, double copperThicknessMm = 0.035, double epsilonR = 4.5);

    static QJsonObject toJson(const StackupInfo& stackup);
};

#endif // PCB_STACKUP_MANAGER_H
