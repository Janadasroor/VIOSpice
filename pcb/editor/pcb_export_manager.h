// ===== File: pcb/editor/pcb_export_manager.h =====
/*
  * Copyright 2026 Janada Sroor
  * SPDX-License-Identifier: Apache-2.0
  */
#ifndef PCB_EXPORT_MANAGER_H
#define PCB_EXPORT_MANAGER_H

class QGraphicsScene;
class QStatusBar;
class QWidget;

class PCBExportManager {
public:
    static void generateGerbers(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget);
    static void exportPDF(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget);
    static void exportSVG(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget);
    static void exportImage(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget);
    static void exportAssemblyDrawing(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget);
    static void exportIPC2581(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget);
    static void exportODBpp(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget);
    static void exportSTEP(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget);
    static void exportIGES(QGraphicsScene* scene, QStatusBar* statusBar, QWidget* parentWidget);
};

#endif // PCB_EXPORT_MANAGER_H

