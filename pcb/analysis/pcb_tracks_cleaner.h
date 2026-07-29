/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCB_TRACKS_CLEANER_H
#define PCB_TRACKS_CLEANER_H

#include <QGraphicsScene>
#include <QList>
#include <QString>
#include <QUndoStack>

class PCBItem;
class TraceItem;
class ViaItem;
class PadItem;

/**
 * @brief Automated Board & Tracks Cleaner Engine (KiCad 8 Parity)
 * 
 * Performs high-performance automated cleanup on PCB layouts:
 * 1. Zero-Length Trace Removal
 * 2. Duplicate Via Removal
 * 3. Dangling Track Stub Removal
 * 4. Dangling/Unconnected Via Removal
 * 5. Collinear Track Segment Merging
 */
class PCBTracksCleaner {
public:
    struct Options {
        bool deleteZeroLengthTraces = true;
        bool deleteDuplicateVias = true;
        bool deleteDanglingTraces = true;
        bool deleteDanglingVias = true;
        bool mergeCollinearTraces = true;

        Options() {}
    };

    struct Report {
        int zeroLengthTracesRemoved = 0;
        int duplicateViasRemoved = 0;
        int danglingTracesRemoved = 0;
        int danglingViasRemoved = 0;
        int collinearTracesMerged = 0;

        int totalItemsCleaned() const {
            return zeroLengthTracesRemoved + duplicateViasRemoved +
                   danglingTracesRemoved + danglingViasRemoved +
                   collinearTracesMerged;
        }
    };

    static Report cleanBoard(QGraphicsScene* scene, const Options& opts, QUndoStack* undoStack = nullptr);
    static Report cleanBoard(QGraphicsScene* scene, QUndoStack* undoStack = nullptr) {
        return cleanBoard(scene, Options(), undoStack);
    }
};

#endif // PCB_TRACKS_CLEANER_H
