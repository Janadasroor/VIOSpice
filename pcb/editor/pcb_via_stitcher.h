/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCB_VIA_STITCHER_H
#define PCB_VIA_STITCHER_H

class QGraphicsScene;
class QUndoStack;
class QStatusBar;
class QWidget;

class PCBViaStitcher {
public:
    static void performViaStitching(QGraphicsScene* scene, QUndoStack* undoStack, QStatusBar* statusBar, QWidget* parent);
};

#endif // PCB_VIA_STITCHER_H
