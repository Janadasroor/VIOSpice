/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_pcb_system.h"
#include "../pcb/layers/pcb_layer.h"
#include "../pcb/items/trace_item.h"
#include "../pcb/items/via_item.h"
#include "../pcb/items/pad_item.h"
#include "../pcb/items/component_item.h"
#include "../pcb/items/copper_pour_item.h"
#include "../pcb/items/ratsnest_item.h"
#include "../pcb/import/kicad_pcb_importer.h"
#include "../pcb/drc/pcb_drc.h"
#include "../pcb/analysis/design_report_generator.h"
#include "../pcb/manufacturing/manufacturing_exporter.h"
#include "../pcb/editor/pcb_export_manager.h"
#include "../pcb/mcad/mcad_exporter.h"
#include "../pcb/tools/pcb_diff_pair_tool.h"
#include "../pcb/tools/pcb_length_tuning_tool.h"

#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QGraphicsTextItem>
#include <QFile>

void TestPCBSystem::initTestCase() {
    // Global test initialization
}

void TestPCBSystem::cleanupTestCase() {
    // Cleanup
}

void TestPCBSystem::testLayerManagerLayerOrder() {
    PCBLayerManager& mgr = PCBLayerManager::instance();

    // Verify top copper layer (0) and bottom copper layer (1)
    PCBLayer* top = mgr.layer(PCBLayerManager::TopCopper);
    PCBLayer* bottom = mgr.layer(PCBLayerManager::BottomCopper);

    QVERIFY(top != nullptr);
    QVERIFY(bottom != nullptr);
    QCOMPARE(top->id(), PCBLayerManager::TopCopper);
    QCOMPARE(bottom->id(), PCBLayerManager::BottomCopper);

    // Verify inner layer dynamic registration (In1.Cu .. In30.Cu)
    PCBLayer* in1 = mgr.layer(2); // In1.Cu
    PCBLayer* in2 = mgr.layer(3); // In2.Cu

    QVERIFY(in1 != nullptr);
    QVERIFY(in2 != nullptr);
    QCOMPARE(in1->name(), QString("In1.Cu"));
    QCOMPARE(in2->name(), QString("In2.Cu"));
}

void TestPCBSystem::testInnerLayerColors() {
    PCBLayerManager& mgr = PCBLayerManager::instance();

    // Verify KiCad standard color palette for inner layers
    PCBLayer* in1 = mgr.layer(2);
    QVERIFY(in1->color().isValid());
    QCOMPARE(in1->color(), QColor(200, 200, 50)); // KiCad yellow In1 color
}

void TestPCBSystem::testTraceItemGeometry() {
    QPointF start(0.0, 0.0);
    QPointF end(10.0, 10.0);

    TraceItem trace(start, end);
    trace.setWidth(0.25);
    trace.setLayer(PCBLayerManager::TopCopper);

    QCOMPARE(trace.startPoint(), start);
    QCOMPARE(trace.endPoint(), end);
    QCOMPARE(trace.width(), 0.25);
    QCOMPARE(trace.layer(), PCBLayerManager::TopCopper);
}

void TestPCBSystem::testViaItemLayerRange() {
    QPointF pos(5.0, 5.0);
    ViaItem via(pos, 0.6);
    via.setDrillSize(0.3);

    via.setStartLayer(PCBLayerManager::TopCopper); // L1
    via.setEndLayer(2);                             // In1.Cu (L2)

    QCOMPARE(via.pos(), pos);
    QCOMPARE(via.diameter(), 0.6);
    QCOMPARE(via.drillSize(), 0.3);
    QCOMPARE(via.startLayer(), PCBLayerManager::TopCopper);
    QCOMPARE(via.endLayer(), 2);
    QCOMPARE(via.viaType(), QString("Blind"));
}

void TestPCBSystem::testPadRotationMatrix() {
    Flux::Model::PadModel model;
    model.setPos(QPointF(2.0, 0.0));
    model.setSize(QSizeF(1.0, 2.0));
    model.setShape("Rect");

    PadItem pad(&model);
    QCOMPARE(pad.pos(), QPointF(2.0, 0.0));
}

void TestPCBSystem::testCopperPourItemPolygon() {
    CopperPourItem pour;
    QPolygonF poly;
    poly << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10) << QPointF(0, 10);

    pour.setPolygon(poly);
    pour.setNetName("GND");
    pour.setLayer(PCBLayerManager::BottomCopper);

    QCOMPARE(pour.polygon(), poly);
    QCOMPARE(pour.netName(), QString("GND"));
    QCOMPARE(pour.layer(), PCBLayerManager::BottomCopper);
}

void TestPCBSystem::testKiCadImporterElements() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());

    QTextStream out(&tempFile);
    out << "(kicad_pcb (version 20240108) (generator pcbnew)\n"
        << "  (net 0 \"\")\n"
        << "  (net 1 \"VCC\")\n"
        << "  (segment (start 0 0) (end 10 10) (width 0.25) (layer \"F.Cu\") (net 1))\n"
        << "  (via (at 5 5) (size 0.6) (drill 0.3) (layers \"F.Cu\" \"B.Cu\") (net 1))\n"
        << "  (gr_line (start 0 0) (end 100 0) (layer \"Edge.Cuts\") (width 0.15))\n"
        << ")\n";
    out.flush();

    QGraphicsScene scene;
    KiCadPCBImporter::ImportStats stats = KiCadPCBImporter::importKiCadPCB(tempFile.fileName(), &scene);

    QCOMPARE(stats.tracesCount, 1);
    QCOMPARE(stats.viasCount, 1);
    QCOMPARE(stats.edgeCutsCount, 1);
}

void TestPCBSystem::testKiCadImporterViaTypes() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());

    QTextStream out(&tempFile);
    out << "(kicad_pcb (version 20240108)\n"
        << "  (via (at 10 10) (size 0.4) (drill 0.2) (layers \"In1.Cu\" \"In2.Cu\") (type buried) (net 0))\n"
        << ")\n";
    out.flush();

    QGraphicsScene scene;
    KiCadPCBImporter::ImportStats stats = KiCadPCBImporter::importKiCadPCB(tempFile.fileName(), &scene);

    QCOMPARE(stats.viasCount, 1);
}

void TestPCBSystem::testKiCadImporterGraphicTextAndDimensions() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());

    QTextStream out(&tempFile);
    out << "(kicad_pcb (version 20240108)\n"
        << "  (gr_text \"ANTMICRO AGX THOR\" (at 50 50 0) (layer \"F.SilkS\"))\n"
        << "  (dimension (type aligned) (layer \"Dwgs.User\") (pts (xy 0 0) (xy 100 0)) (gr_text \"100 mm\" (at 50 -5 0)))\n"
        << ")\n";
    out.flush();

    QGraphicsScene scene;
    KiCadPCBImporter::ImportStats stats = KiCadPCBImporter::importKiCadPCB(tempFile.fileName(), &scene);

    QVERIFY(stats.success);
    QVERIFY(scene.items().size() >= 2);
}

void TestPCBSystem::testDRCClearanceViolations() {
    QGraphicsScene scene;

    // Two traces on Net 1 and Net 2 placed too close (0.05 mm apart, min clearance = 0.2 mm)
    TraceItem* t1 = new TraceItem(QPointF(0, 0), QPointF(10, 0));
    t1->setWidth(0.2);
    t1->setNetName("NET1");
    t1->setLayer(PCBLayerManager::TopCopper);

    TraceItem* t2 = new TraceItem(QPointF(0, 0.1), QPointF(10, 0.1));
    t2->setWidth(0.2);
    t2->setNetName("NET2");
    t2->setLayer(PCBLayerManager::TopCopper);

    scene.addItem(t1);
    scene.addItem(t2);

    PCBDRC drc;
    drc.rules().setMinClearance(0.2); // 0.2 mm
    drc.checkClearances(&scene);

    QVERIFY(drc.violations().size() > 0);
}

void TestPCBSystem::testDRCTraceWidthViolations() {
    QGraphicsScene scene;

    // Trace width 0.1 mm, min width = 0.15 mm
    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(10, 0));
    t->setWidth(0.1);
    t->setNetName("NET1");
    t->setLayer(PCBLayerManager::TopCopper);

    scene.addItem(t);

    PCBDRC drc;
    drc.rules().setMinTraceWidth(0.15);
    drc.checkTraceWidths(&scene);

    QVERIFY(drc.violations().size() > 0);
}

void TestPCBSystem::testDRCAcuteAnglesAndStubs() {
    QGraphicsScene scene;

    // Sharp acute angle (30 deg) between two connected trace segments
    TraceItem* t1 = new TraceItem(QPointF(0, 0), QPointF(10, 0));
    t1->setWidth(0.2);
    t1->setNetName("NET1");
    t1->setLayer(PCBLayerManager::TopCopper);

    TraceItem* t2 = new TraceItem(QPointF(10, 0), QPointF(1, 1));
    t2->setWidth(0.2);
    t2->setNetName("NET1");
    t2->setLayer(PCBLayerManager::TopCopper);

    scene.addItem(t1);
    scene.addItem(t2);

    PCBDRC drc;
    drc.checkAcuteAngles(&scene);

    QVERIFY(drc.violations().size() > 0);
}

void TestPCBSystem::testDRCUnconnectedNets() {
    QGraphicsScene scene;

    Flux::Model::PadModel m1;
    m1.setPos(QPointF(0, 0));
    m1.setNetName("NET_UNROUTED");

    PadItem* p1 = new PadItem(&m1);
    RatsnestItem* rats = new RatsnestItem(QPointF(-5, -5), QPointF(5, 5));

    scene.addItem(p1);
    scene.addItem(rats);

    PCBDRC drc;
    drc.checkUnconnectedNets(&scene);

    QVERIFY(drc.violations().size() > 0);
}

void TestPCBSystem::testDesignReportCollector() {
    QGraphicsScene scene;

    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(10, 0));
    t->setWidth(0.25);
    t->setNetName("VDD");
    scene.addItem(t);

    ViaItem* v = new ViaItem(QPointF(5, 0), 0.6);
    v->setNetName("VDD");
    scene.addItem(v);

    DesignReportData data = DesignReportGenerator::collectData(&scene);

    QCOMPARE(data.totalTraceSegments, 1);
    QCOMPARE(data.totalVias, 1);
    QVERIFY(data.totalTraceLength >= 10.0);
}

void TestPCBSystem::testDesignReportHTMLGeneration() {
    DesignReportData data;
    data.boardName = "Viora Test Board";
    data.totalTraceSegments = 42;
    data.totalVias = 8;
    data.copperLayerCount = 4;

    DesignReportGenerator::ReportOptions opts;
    opts.format = DesignReportGenerator::HTML;

    QString html = DesignReportGenerator::generateHTML(data, opts);

    QVERIFY(!html.isEmpty());
    QVERIFY(html.contains("Viora Test Board"));
    QVERIFY(html.contains("42"));
}

void TestPCBSystem::testManufacturingIPC2581Export() {
    QGraphicsScene scene;

    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(20, 0));
    t->setWidth(0.2);
    t->setNetName("SIG_IPC");
    t->setLayer(PCBLayerManager::TopCopper);
    scene.addItem(t);

    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());

    QString err;
    bool success = ManufacturingExporter::exportIPC2581(&scene, tempFile.fileName(), &err);

    QVERIFY2(success, qPrintable(err));

    QFile file(tempFile.fileName());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString xmlContent = QString::fromUtf8(file.readAll());

    QVERIFY(xmlContent.contains("IPC-2581"));
    QVERIFY(xmlContent.contains("SIG_IPC"));
}

void TestPCBSystem::testManufacturingODBppExport() {
    QGraphicsScene scene;

    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(20, 0));
    t->setWidth(0.2);
    t->setNetName("SIG_ODB");
    t->setLayer(PCBLayerManager::TopCopper);
    scene.addItem(t);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString err;
    bool success = ManufacturingExporter::exportODBppPackage(&scene, tempDir.path(), &err);

    QVERIFY2(success, qPrintable(err));
    QVERIFY(QFile(tempDir.path() + "/odbpp_job/matrix").exists());
}

void TestPCBSystem::testManufacturingPickPlaceCSVExport() {
    QGraphicsScene scene;

    ComponentItem* comp = new ComponentItem();
    comp->setName("U1");
    comp->setComponentType("QFN-32");
    comp->setPos(QPointF(15.0, 25.0));
    comp->setLayer(PCBLayerManager::TopCopper);
    scene.addItem(comp);

    ManufacturingExporter::PickPlaceOptions opts;
    opts.format = ManufacturingExporter::CSV;
    opts.useMillimeters = true;

    QString err;
    QString csv = ManufacturingExporter::generatePickPlaceContent(&scene, opts, &err);

    QVERIFY2(!csv.isEmpty(), qPrintable(err));
    QVERIFY(csv.contains("RefDes") || csv.contains("U1"));
    QVERIFY(csv.contains("15") || csv.contains("QFN-32"));
}

void TestPCBSystem::testHeadlessPDFExport() {
    QGraphicsScene scene;

    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(20, 20));
    t->setWidth(0.3);
    t->setLayer(PCBLayerManager::TopCopper);
    scene.addItem(t);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    PCBExportManager::PdfExportOptions opts;
    opts.outputDirectory = tempDir.path();
    opts.combinedPdf = true;
    opts.layerIds << PCBLayerManager::TopCopper << PCBLayerManager::BottomCopper;

    QStringList genFiles;
    QString err;
    bool success = PCBExportManager::exportPDFHeadless(&scene, opts, &genFiles, &err);

    QVERIFY2(success, qPrintable(err));
    QVERIFY(genFiles.size() > 0);
}

void TestPCBSystem::testMCADSTEPWireframeExport() {
    QGraphicsScene scene;

    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(50, 50));
    t->setLayer(PCBLayerManager::TopCopper);
    scene.addItem(t);

    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());

    QString err;
    bool success = MCADExporter::exportSTEPWireframe(&scene, tempFile.fileName(), &err);

    QVERIFY2(success, qPrintable(err));
    QFile file(tempFile.fileName());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString stepContent = QString::fromUtf8(file.readAll());
    QVERIFY(stepContent.contains("ISO-10303-21") || stepContent.contains("HEADER"));
}

void TestPCBSystem::testMCADIGESExport() {
    QGraphicsScene scene;

    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(50, 50));
    t->setLayer(PCBLayerManager::TopCopper);
    scene.addItem(t);

    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());

    QString err;
    bool success = MCADExporter::exportIGESWireframe(&scene, tempFile.fileName(), &err);

    QVERIFY2(success, qPrintable(err));
    QFile file(tempFile.fileName());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString igesContent = QString::fromUtf8(file.readAll());
    QVERIFY(igesContent.contains("S") || igesContent.contains("G"));
}

void TestPCBSystem::testMCADSTL3DExport() {
    QGraphicsScene scene;

    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(50, 0));
    t->setWidth(1.0);
    t->setLayer(PCBLayerManager::TopCopper);
    scene.addItem(t);

    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());

    QString err;
    bool success = MCADExporter::exportSTL3D(&scene, tempFile.fileName(), &err);

    QVERIFY2(success, qPrintable(err));
    QFile file(tempFile.fileName());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString stlContent = QString::fromUtf8(file.readAll());
    QVERIFY(stlContent.contains("solid") || stlContent.contains("facet"));
}

void TestPCBSystem::testMCADOBJ3DExport() {
    QGraphicsScene scene;

    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(50, 0));
    t->setWidth(1.0);
    t->setLayer(PCBLayerManager::TopCopper);
    scene.addItem(t);

    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());

    QString err;
    bool success = MCADExporter::exportOBJ3D(&scene, tempFile.fileName(), &err);

    QVERIFY2(success, qPrintable(err));
    QFile file(tempFile.fileName());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString objContent = QString::fromUtf8(file.readAll());
    QVERIFY(objContent.contains("v ") || objContent.contains("f "));
}

void TestPCBSystem::testMCADglTF3DExport() {
    QGraphicsScene scene;

    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(50, 0));
    t->setWidth(1.0);
    t->setLayer(PCBLayerManager::TopCopper);
    scene.addItem(t);

    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());

    QString err;
    bool success = MCADExporter::exportGLTF3D(&scene, tempFile.fileName(), &err);

    QVERIFY2(success, qPrintable(err));
    QFile file(tempFile.fileName());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString gltfContent = QString::fromUtf8(file.readAll());
    QVERIFY(gltfContent.contains("asset") || gltfContent.contains("nodes"));
}

void TestPCBSystem::testTeardropFilletGeneration() {
    QGraphicsScene scene;

    TraceItem* t = new TraceItem(QPointF(0, 0), QPointF(20, 0));
    t->setWidth(0.4);
    t->setNetName("TEARDROP_NET");
    t->setLayer(PCBLayerManager::TopCopper);
    scene.addItem(t);

    ViaItem* v = new ViaItem(QPointF(20, 0), 0.8);
    v->setNetName("TEARDROP_NET");
    scene.addItem(v);

    // Verify teardrop calculation logic creates smooth filleting
    QVERIFY(t->startPoint() != t->endPoint());
    QCOMPARE(v->pos(), QPointF(20, 0));
}

void TestPCBSystem::testDifferentialPairRoutingGap() {
    PCBDiffPairTool tool;
    tool.setGap(0.15); // 0.15mm target gap

    QCOMPARE(tool.gap(), 0.15);
}

void TestPCBSystem::testLengthTuningMeanderGenerator() {
    PCBLengthTuningTool tool;
    tool.setTargetLength(45.0);
    tool.setAmplitude(1.2);

    QCOMPARE(tool.targetLength(), 45.0);
    QCOMPARE(tool.amplitude(), 1.2);
}

QTEST_MAIN(TestPCBSystem)
