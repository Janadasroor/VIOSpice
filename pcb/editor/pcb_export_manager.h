// ===== File: pcb/editor/pcb_export_manager.h =====
/*
  * Copyright 2026 Janada Sroor
  * SPDX-License-Identifier: Apache-2.0
  */
#ifndef PCB_EXPORT_MANAGER_H
#define PCB_EXPORT_MANAGER_H

#include <QString>
#include <QStringList>
#include <QList>

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

    struct PdfExportOptions {
        QString outputDirectory = QStringLiteral("./output");
        QList<int> layerIds;
        bool oneToOne = true;
        bool blackAndWhite = false;
        bool combinedPdf = true;
        int pageSizeMode = 0; // 0=A4, 1=A3, 2=A2, 3=Letter, -1=BoardSize
        int orientationMode = 0; // 0=Auto, 1=Portrait, 2=Landscape
        double marginMm = 10.0;
        bool titleBlock = true;
        bool mirrorPlot = false;
        int drillMarksMode = 1; // 0=None, 1=Small, 2=Full
    };

    static bool exportPDFHeadless(QGraphicsScene* scene, const PdfExportOptions& opts, QStringList* generatedFiles = nullptr, QString* errorMsg = nullptr);
};

#endif // PCB_EXPORT_MANAGER_H

