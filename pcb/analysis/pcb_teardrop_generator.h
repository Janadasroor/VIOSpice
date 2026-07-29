/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCB_TEARDROP_GENERATOR_H
#define PCB_TEARDROP_GENERATOR_H

#include <QGraphicsScene>
#include <QUndoStack>
#include <QPolygonF>

class TeardropItem;

/**
 * @brief Smart Teardrop & Fillet Generator Engine (KiCad 8 Parity)
 * 
 * Generates smooth curved/filleted teardrop copper transitions at pad-to-trace,
 * via-to-trace, and track-to-track intersections.
 */
class PCBTeardropGenerator {
public:
    enum class TeardropShape {
        Curved,   ///< Smooth Bezier curve transition (KiCad default)
        Fillet,   ///< Straight chamfered fillet
        Arc       ///< Circular arc transition
    };

    struct Options {
        bool includePads = true;
        bool includeVias = true;
        bool includeTracks = true;
        TeardropShape shape = TeardropShape::Curved;
        double lengthRatio = 1.5;   ///< Teardrop length relative to trace width
        double widthRatio = 0.8;    ///< Teardrop width relative to pad/via diameter
        double minTraceWidth = 0.1; ///< Minimum trace width to apply teardrops (mm)

        Options() {}
    };

    struct Report {
        int padTeardropsAdded = 0;
        int viaTeardropsAdded = 0;
        int trackTeardropsAdded = 0;
        int totalTeardropsAdded() const {
            return padTeardropsAdded + viaTeardropsAdded + trackTeardropsAdded;
        }
    };

    static Report addTeardrops(QGraphicsScene* scene, const Options& opts, QUndoStack* undoStack = nullptr);
    static Report addTeardrops(QGraphicsScene* scene, QUndoStack* undoStack = nullptr) {
        return addTeardrops(scene, Options(), undoStack);
    }
    static int removeTeardrops(QGraphicsScene* scene, QUndoStack* undoStack = nullptr);
};

#endif // PCB_TEARDROP_GENERATOR_H
