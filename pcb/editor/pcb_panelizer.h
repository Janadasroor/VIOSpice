/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCB_PANELIZER_H
#define PCB_PANELIZER_H

class QGraphicsScene;
class QStatusBar;
class QUndoStack;
class QWidget;

class PCBPanelizer {
public:
    static void panelize(QGraphicsScene* scene, QStatusBar* statusBar, QUndoStack* undoStack, QWidget* parentWidget);
};

#endif // PCB_PANELIZER_H
