/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCB_ARRAY_GENERATOR_H
#define PCB_ARRAY_GENERATOR_H

#include <QGraphicsScene>
#include <QUndoStack>
#include <QPointF>
#include <QList>

class PCBItem;

/**
 * @brief Grid & Circular Array Placement Generator (KiCad 8 Parity)
 * 
 * Generates rectangular matrix and circular/polar array patterns for
 * components, pads, and vias.
 */
class PCBArrayGenerator {
public:
    enum class ArrayMode {
        Rectangular,
        Circular
    };

    struct Options {
        ArrayMode mode = ArrayMode::Rectangular;
        
        // Rectangular parameters
        int cols = 3;
        int rows = 3;
        double deltaX = 5.0; // mm
        double deltaY = 5.0; // mm
        
        // Circular parameters
        int count = 8;
        double radius = 15.0;     // mm
        double startAngleDeg = 0.0;
        double spanAngleDeg = 360.0;
        bool rotateItems = true;

        Options() {}
    };

    struct Report {
        int itemsCreated = 0;
    };

    static Report createArray(QGraphicsScene* scene, const QList<PCBItem*>& sourceItems, const Options& opts, QUndoStack* undoStack = nullptr);
};

#endif // PCB_ARRAY_GENERATOR_H
