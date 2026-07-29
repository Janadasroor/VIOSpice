/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef KICAD_PCB_IMPORTER_H
#define KICAD_PCB_IMPORTER_H

#include <QString>
#include <QGraphicsScene>
#include <QList>
#include <QMap>

/**
 * @brief Full KiCad PCB Importer for KiCad 5, 6, 7, and 8 .kicad_pcb files.
 * Parses S-expression syntax, extracts nets, traces, vias, footprints, and zones,
 * and maps footprints to VioraEDA footprint models.
 */
class KiCadPCBImporter {
public:
    struct ImportStats {
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
     * @brief Import a KiCad PCB file directly into a QGraphicsScene.
     */
    static ImportStats importKiCadPCB(const QString& filePath, QGraphicsScene* scene);
};

#endif // KICAD_PCB_IMPORTER_H
