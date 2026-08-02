/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_PCB_SYSTEM_H
#define TEST_PCB_SYSTEM_H

#include <QtTest/QtTest>
#include <QGraphicsScene>

class TestPCBSystem : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 1. Layer Manager & Color Stackup Tests
    void testLayerManagerLayerOrder();
    void testInnerLayerColors();

    // 2. PCB Items & Geometry Unit Tests
    void testTraceItemGeometry();
    void testViaItemLayerRange();
    void testPadRotationMatrix();

    // 3. Copper Pour & Polygons (Zone Filler) Tests
    void testCopperPourItemPolygon();

    // 4. KiCad S-Expression Importer Suite Tests
    void testKiCadImporterElements();
    void testKiCadImporterViaTypes();
    void testKiCadImporterGraphicTextAndDimensions();

    // 5. DRC & Manufacturing Rule Verification Tests (KiCad DRC Parity)
    void testDRCClearanceViolations();
    void testDRCTraceWidthViolations();
    void testDRCAcuteAnglesAndStubs();
    void testDRCUnconnectedNets();

    // 6. PCB Design Analysis & Report Generation Unit Tests
    void testDesignReportCollector();
    void testDesignReportHTMLGeneration();

    // 7. Manufacturing Export Unit Tests (IPC-2581, ODB++, Pick & Place CSV, PDF Export)
    void testManufacturingIPC2581Export();
    void testManufacturingODBppExport();
    void testManufacturingPickPlaceCSVExport();
    void testHeadlessPDFExport();

    // 8. 3D MCAD Model Exporter Suite Tests (STEP, IGES, STL, OBJ, glTF)
    void testMCADSTEPWireframeExport();
    void testMCADIGESExport();
    void testMCADSTL3DExport();
    void testMCADOBJ3DExport();
    void testMCADglTF3DExport();

    // 9. Interactive Push-and-Shove & Teardrop Fillet Generator Tests
    void testTeardropFilletGeneration();

    // 10. Differential Pair & Length Tuning Tool Tests
    void testDifferentialPairRoutingGap();
    void testLengthTuningMeanderGenerator();

    // 11. ECO Footprint Suggestion & On-Board Placement Tests
    void testECOSuggestFootprints();
    void testECOPlacementInsideBoard();
};

#endif // TEST_PCB_SYSTEM_H
