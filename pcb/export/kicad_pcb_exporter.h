/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KICAD_PCB_EXPORTER_H
#define KICAD_PCB_EXPORTER_H

#include <QString>
#include <QGraphicsScene>
#include <QList>

/**
 * @brief Exporter for KiCad 8 .kicad_pcb files.
 * Exports VioSpice PCB scene items (nets, traces, vias, footprints, pads, zones, edge cuts)
 * directly to standard KiCad 8 S-expression syntax.
 */
class KiCadPCBExporter {
public:
    struct ExportStats {
        int netsCount = 0;
        int tracesCount = 0;
        int viasCount = 0;
        int footprintsCount = 0;
        int zonesCount = 0;
        int edgeCutsCount = 0;
        bool success = false;
        QString error;
    };

    /**
     * @brief Export a QGraphicsScene to a KiCad 8 .kicad_pcb file.
     * @param filePath Target .kicad_pcb output path
     * @param scene Source PCB scene
     * @return ExportStats summary
     */
    static ExportStats exportKiCadPCB(const QString& filePath, QGraphicsScene* scene);
};

#endif // KICAD_PCB_EXPORTER_H
