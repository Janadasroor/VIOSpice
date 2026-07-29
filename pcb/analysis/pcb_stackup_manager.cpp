/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcb_stackup_manager.h"
#include <QJsonArray>
#include <cmath>

PCBStackupManager::StackupInfo PCBStackupManager::createStandardStackup(int copperLayerCount, double boardThicknessMm) {
    StackupInfo info;
    info.copperLayers = copperLayerCount;
    info.totalThicknessMm = boardThicknessMm;

    SubstrateLayer fMask{"Top Soldermask", "Soldermask", 0.015, 3.8, 0.0};
    SubstrateLayer fCu{"Top Copper", "Copper", 0.035, 1.0, 1.0};
    SubstrateLayer core{"FR4 Core", "Core", boardThicknessMm - (0.035 * copperLayerCount) - 0.030, 4.5, 0.0};
    SubstrateLayer bCu{"Bottom Copper", "Copper", 0.035, 1.0, 1.0};
    SubstrateLayer bMask{"Bottom Soldermask", "Soldermask", 0.015, 3.8, 0.0};

    info.layers << fMask << fCu << core << bCu << bMask;
    return info;
}

double PCBStackupManager::calculateMicrostripZ0(double traceWidthMm, double dielectricThicknessMm, double copperThicknessMm, double epsilonR) {
    if (traceWidthMm <= 0.001 || dielectricThicknessMm <= 0.001) return 50.0;
    
    // IPC-2141 Microstrip Impedance Formula
    double w = traceWidthMm;
    double h = dielectricThicknessMm;
    double t = copperThicknessMm;

    double num = 5.98 * h;
    double den = (0.8 * w) + t;
    if (den <= 1e-6) return 50.0;

    double z0 = (87.0 / std::sqrt(epsilonR + 1.41)) * std::log(num / den);
    return std::max(10.0, std::min(300.0, z0));
}

double PCBStackupManager::calculateDiffImpedance(double traceWidthMm, double spacingMm, double dielectricThicknessMm, double copperThicknessMm, double epsilonR) {
    double z0 = calculateMicrostripZ0(traceWidthMm, dielectricThicknessMm, copperThicknessMm, epsilonR);
    if (spacingMm <= 0.001 || dielectricThicknessMm <= 0.001) return 2.0 * z0;

    double s = spacingMm;
    double h = dielectricThicknessMm;

    double couplingFactor = 1.0 - (0.48 * std::exp(-0.96 * (s / h)));
    double zDiff = 2.0 * z0 * couplingFactor;
    return std::max(20.0, std::min(500.0, zDiff));
}

QJsonObject PCBStackupManager::toJson(const StackupInfo& stackup) {
    QJsonObject obj;
    obj["copperLayers"] = stackup.copperLayers;
    obj["totalThicknessMm"] = stackup.totalThicknessMm;
    
    QJsonArray array;
    for (const auto& l : stackup.layers) {
        QJsonObject layerObj;
        layerObj["name"] = l.name;
        layerObj["type"] = l.type;
        layerObj["thicknessMm"] = l.thicknessMm;
        layerObj["epsilonR"] = l.epsilonR;
        layerObj["copperWeightOz"] = l.copperWeightOz;
        array.append(layerObj);
    }
    obj["layers"] = array;
    return obj;
}
