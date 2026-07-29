/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCB_VIA_FENCE_GENERATOR_H
#define PCB_VIA_FENCE_GENERATOR_H

#include <QGraphicsScene>
#include <QUndoStack>
#include <QString>
#include <QList>

class ViaItem;
class TraceItem;

/**
 * @brief RF & High-Speed Via Fencing Generator (KiCad 8 Parity)
 * 
 * Automatically generates dual ground via fences along trace pairs or
 * board boundaries to contain electromagnetic interference (EMI).
 */
class PCBViaFenceGenerator {
public:
    struct Options {
        QString netName = "GND";
        double viaPitch = 1.5;     ///< Distance between adjacent vias (mm)
        double offsetDistance = 1.0; ///< Clearance offset from trace edge (mm)
        double viaDiameter = 0.8;  ///< Via pad diameter (mm)
        double drillDiameter = 0.4;///< Via drill hole (mm)
        bool dualSided = true;     ///< Place fences on both sides of trace

        Options() {}
    };

    struct Report {
        int viasPlaced = 0;
    };

    static Report generateViaFence(QGraphicsScene* scene, const QList<TraceItem*>& targetTraces, const Options& opts = Options(), QUndoStack* undoStack = nullptr);
};

#endif // PCB_VIA_FENCE_GENERATOR_H
