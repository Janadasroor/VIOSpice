/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MANUFACTURING_EXPORTER_H
#define MANUFACTURING_EXPORTER_H

#include <QString>

class QGraphicsScene;

class ManufacturingExporter {
public:
    static bool exportIPC2581(QGraphicsScene* scene, const QString& filePath, QString* error = nullptr);
    static bool exportODBppPackage(QGraphicsScene* scene, const QString& outputDirectory, QString* error = nullptr);

    // Pick-and-Place / Centroid file export
    enum PickPlaceFormat {
        CSV,       // Comma-separated values
        TSV        // Tab-separated values (for Excel)
    };

    struct PickPlaceOptions {
        PickPlaceFormat format = CSV;
        bool includeTopSide = true;
        bool includeBottomSide = true;
        bool includeFiducials = false;
        bool includeTestPoints = false;
        bool useMillimeters = true;  // false = inches
        bool includeValue = true;
        bool includeFootprint = true;
    };

    static bool exportPickPlace(QGraphicsScene* scene, const QString& filePath,
                                const PickPlaceOptions& options,
                                QString* error = nullptr);

    static QString generatePickPlaceContent(QGraphicsScene* scene,
                                            const PickPlaceOptions& options,
                                            QString* error = nullptr);

    // One-Click Manufacturing Package Generator
    enum FabricatorPreset {
        JLCPCB,
        PCBWay,
        Eurocircuits,
        GenericGerber
    };

    struct ManufacturingPackageOptions {
        FabricatorPreset preset = JLCPCB;
        bool includeBOM = true;
        bool includeCPL = true;
        bool includeGerbers = true;
        bool includeDrill = true;
        bool zipPackage = true;
    };

    static bool exportManufacturingPackage(QGraphicsScene* scene,
                                         const QString& outputPath,
                                         const ManufacturingPackageOptions& options,
                                         QString* error = nullptr);

    static QString generateBOMCSV(QGraphicsScene* scene);
    static QString generateCPLCSV(QGraphicsScene* scene, FabricatorPreset preset = JLCPCB);
};

#endif // MANUFACTURING_EXPORTER_H
