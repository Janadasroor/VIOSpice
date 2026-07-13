/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_serialization_and_cloning.h"
#include "../footprints/models/footprint_schema.h"

using namespace Flux::Model;

#include "../schematic/factories/schematic_item_registry.h"

void TestSerializationAndCloning::initTestCase() {
    SchematicItemRegistry::registerBuiltInItems();
}

// Phase 1 & 2 Tests
void TestSerializationAndCloning::testFootprintPrimitiveSerialization_StringNames() {
    // Create a primitive (e.g., Pad)
    FootprintPrimitive prim = FootprintPrimitive::createPad(QPointF(1.0, 2.0), "1", "Rect", QSizeF(1.5, 1.5), 0.1);
    prim.layer = FootprintPrimitive::Top_Copper;

    // Serialize to JSON
    QJsonObject json = prim.toJson();

    // Verify string enum serialization
    QCOMPARE(json["type"].toString(), QString("Pad"));
    QCOMPARE(json["layer"].toString(), QString("Top_Copper"));
    
    // Verify properties match
    QJsonObject data = json["data"].toObject();
    QCOMPARE(data[Schema::Number].toString(), QString("1"));
    QCOMPARE(data[Schema::Shape].toString(), QString("Rect"));
    QCOMPARE(data[Schema::X].toDouble(), 1.0);
    QCOMPARE(data[Schema::Y].toDouble(), 2.0);
}

void TestSerializationAndCloning::testFootprintPrimitiveDeserialization_LegacyFallback() {
    // 1. Test legacy stringified integer fallback
    QJsonObject legacyJson1;
    legacyJson1["type"] = "6"; // Pad index
    legacyJson1["layer"] = "3"; // Top_Copper index
    QJsonObject data1;
    data1["number"] = "1A";
    legacyJson1["data"] = data1;

    FootprintPrimitive prim1 = FootprintPrimitive::fromJson(legacyJson1);
    QCOMPARE(prim1.type, FootprintPrimitive::Pad);
    QCOMPARE(prim1.layer, FootprintPrimitive::Top_Copper);
    QCOMPARE(prim1.data["number"].toString(), QString("1A"));

    // 2. Test legacy raw integer fallback
    QJsonObject legacyJson2;
    legacyJson2["type"] = static_cast<double>(FootprintPrimitive::Arc);
    legacyJson2["layer"] = static_cast<double>(FootprintPrimitive::Top_Silkscreen);
    QJsonObject data2;
    data2["radius"] = 2.5;
    legacyJson2["data"] = data2;

    FootprintPrimitive prim2 = FootprintPrimitive::fromJson(legacyJson2);
    QCOMPARE(prim2.type, FootprintPrimitive::Arc);
    QCOMPARE(prim2.layer, FootprintPrimitive::Top_Silkscreen);
    QCOMPARE(prim2.data["radius"].toDouble(), 2.5);
}

void TestSerializationAndCloning::testFootprintSchemaConstants() {
    // Simple schema constant sanity checks
    QCOMPARE(QString(Schema::Width), QString("width"));
    QCOMPARE(QString(Schema::Height), QString("height"));
    QCOMPARE(QString(Schema::DrillSize), QString("drill_size"));
    QCOMPARE(QString(Schema::Rotation), QString("rotation"));
}

// Phase 3 & 4 Tests
void TestSerializationAndCloning::testTraceModelCloning() {
    TraceModel original;
    original.setStart(QPointF(0, 0));
    original.setEnd(QPointF(10, 10));
    original.setWidth(0.25);
    original.setLayer(1);
    original.setNetName("GND");

    TraceModel* cloned = original.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned->id() == original.id());
    QCOMPARE(cloned->start(), original.start());
    QCOMPARE(cloned->end(), original.end());
    QCOMPARE(cloned->width(), original.width());
    QCOMPARE(cloned->layer(), original.layer());
    QCOMPARE(cloned->netName(), original.netName());

    delete cloned;
}

void TestSerializationAndCloning::testViaModelCloning() {
    ViaModel original;
    original.setPos(QPointF(5, 5));
    original.setDiameter(1.2);
    original.setDrillSize(0.6);
    original.setStartLayer(1);
    original.setEndLayer(2);
    original.setNetName("VCC");

    ViaModel* cloned = original.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned->id() == original.id());
    QCOMPARE(cloned->pos(), original.pos());
    QCOMPARE(cloned->diameter(), original.diameter());
    QCOMPARE(cloned->drillSize(), original.drillSize());
    QCOMPARE(cloned->startLayer(), original.startLayer());
    QCOMPARE(cloned->endLayer(), original.endLayer());
    QCOMPARE(cloned->netName(), original.netName());

    delete cloned;
}

void TestSerializationAndCloning::testPadModelCloning() {
    PadModel original;
    original.setPos(QPointF(2, 3));
    original.setSize(QSizeF(2.0, 1.5));
    original.setRotation(45.0);
    original.setShape("Oval");
    original.setLayer(0);
    original.setNetName("CLK");
    original.setNumber("A1");

    PadModel* cloned = original.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned->id() == original.id());
    QCOMPARE(cloned->pos(), original.pos());
    QCOMPARE(cloned->size(), original.size());
    QCOMPARE(cloned->rotation(), original.rotation());
    QCOMPARE(cloned->shape(), original.shape());
    QCOMPARE(cloned->layer(), original.layer());
    QCOMPARE(cloned->netName(), original.netName());
    QCOMPARE(cloned->number(), original.number());

    delete cloned;
}

void TestSerializationAndCloning::testComponentModelCloning() {
    ComponentModel original;
    original.setPos(QPointF(10, 20));
    original.setRotation(90.0);
    original.setName("U1");
    original.setComponentType("SOIC-8");
    original.setValue("NE555");

    PadModel* p1 = new PadModel();
    p1->setNumber("1");
    original.addPad(p1);

    PadModel* p2 = new PadModel();
    p2->setNumber("2");
    original.addPad(p2);

    ComponentModel* cloned = original.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned->id() == original.id());
    QCOMPARE(cloned->pos(), original.pos());
    QCOMPARE(cloned->rotation(), original.rotation());
    QCOMPARE(cloned->name(), original.name());
    QCOMPARE(cloned->componentType(), original.componentType());
    QCOMPARE(cloned->value(), original.value());

    // Verify child pads cloned properly
    QCOMPARE(cloned->pads().size(), 2);
    QCOMPARE(cloned->pads().at(0)->number(), QString("1"));
    QCOMPARE(cloned->pads().at(1)->number(), QString("2"));
    QVERIFY(cloned->pads().at(0)->id() == p1->id());

    delete cloned;
}

void TestSerializationAndCloning::testCopperPourModelCloning() {
    CopperPourModel original;
    original.setLayer(0);
    original.setNetName("AGND");
    original.setClearance(0.4);
    original.setMinWidth(0.2);
    original.setPriority(2);
    
    QPolygonF poly;
    poly << QPointF(0, 0) << QPointF(0, 50) << QPointF(50, 50) << QPointF(50, 0);
    original.setPolygon(poly);

    CopperPourModel* cloned = original.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned->id() == original.id());
    QCOMPARE(cloned->layer(), original.layer());
    QCOMPARE(cloned->netName(), original.netName());
    QCOMPARE(cloned->clearance(), original.clearance());
    QCOMPARE(cloned->minWidth(), original.minWidth());
    QCOMPARE(cloned->priority(), original.priority());
    QCOMPARE(cloned->polygon(), original.polygon());

    delete cloned;
}

// Phase 5 Tests
#include "../schematic/models/schematic_schema.h"

void TestSerializationAndCloning::testSchematicComponentModelCloning() {
    SchematicComponentModel original;
    original.setId(QUuid::createUuid());
    original.setPos(QPointF(100.5, 200.5));
    original.setRotation(180.0);
    original.setMirroredX(true);
    original.setMirroredY(false);
    original.setUnit(2);
    original.setName("R1");
    original.setValue("4k7");
    original.setReference("R");
    original.setFootprint("R_0805_2012Metric");

    PinModel* p1 = new PinModel();
    p1->id = QUuid::createUuid();
    p1->name = "1";
    p1->number = "1";
    p1->pos = QPointF(-45.0, 0);
    p1->length = 5.0;
    p1->angle = 0.0;
    p1->netName = "Net/1";
    original.addPin(p1);

    SchematicComponentModel* cloned = original.clone();
    QVERIFY(cloned != nullptr);
    QCOMPARE(cloned->id(), original.id());
    QCOMPARE(cloned->pos(), original.pos());
    QCOMPARE(cloned->rotation(), original.rotation());
    QCOMPARE(cloned->isMirroredX(), original.isMirroredX());
    QCOMPARE(cloned->isMirroredY(), original.isMirroredY());
    QCOMPARE(cloned->unit(), original.unit());
    QCOMPARE(cloned->name(), original.name());
    QCOMPARE(cloned->value(), original.value());
    QCOMPARE(cloned->reference(), original.reference());
    QCOMPARE(cloned->footprint(), original.footprint());

    // Verify child pins are cloned correctly
    QCOMPARE(cloned->pins().size(), 1);
    PinModel* clonedPin = cloned->pins().at(0);
    QCOMPARE(clonedPin->id, p1->id);
    QCOMPARE(clonedPin->name, p1->name);
    QCOMPARE(clonedPin->number, p1->number);
    QCOMPARE(clonedPin->pos, p1->pos);
    QCOMPARE(clonedPin->length, p1->length);
    QCOMPARE(clonedPin->angle, p1->angle);
    QCOMPARE(clonedPin->netName, p1->netName);

    delete cloned;
}

void TestSerializationAndCloning::testWireItemSerialization_StringWireType() {
    // 1. Serialize to string
    WireItem wire(WireItem::PowerWire, QPointF(0, 0), QPointF(10, 10));
    QJsonObject json = wire.toJson();
    QCOMPARE(json["wireType"].toString(), QString("Power"));

    // 2. Deserialize string format
    WireItem loadedWire;
    QVERIFY(loadedWire.fromJson(json));
    QCOMPARE(loadedWire.startPoint(), QPointF(0, 0));
    QCOMPARE(loadedWire.endPoint(), QPointF(10, 10));
    
    QJsonObject roundTripJson = loadedWire.toJson();
    QCOMPARE(roundTripJson["wireType"].toString(), QString("Power"));

    // 3. Deserialize legacy integer format fallback
    QJsonObject legacyJson;
    legacyJson["type"] = "Wire";
    legacyJson["wireType"] = 2; // AirWire index
    QJsonArray pts;
    QJsonObject p1; p1["x"] = 0.0; p1["y"] = 0.0; pts.append(p1);
    QJsonObject p2; p2["x"] = 100.0; p2["y"] = 100.0; pts.append(p2);
    legacyJson["points"] = pts;

    WireItem legacyWire;
    QVERIFY(legacyWire.fromJson(legacyJson));
    QJsonObject legacyRoundTrip = legacyWire.toJson();
    QCOMPARE(legacyRoundTrip["wireType"].toString(), QString("Air"));
}

void TestSerializationAndCloning::testSchematicSchemaConstants() {
    QCOMPARE(QString(Flux::SchematicSchema::WireType), QString("wireType"));
    QCOMPARE(QString(Flux::SchematicSchema::Points), QString("points"));
    QCOMPARE(QString(Flux::SchematicSchema::Footprint), QString("footprint"));
}

#include <QTemporaryFile>
#include <QGraphicsScene>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

void TestSerializationAndCloning::testNetlistToSchematic_Basic() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    
    QTextStream out(&tempFile);
    out << "* Basic RC Filter Netlist\n";
    out << "V1 in 0 5V\n";
    out << "R1 in out 10k\n";
    out << "C1 out 0 100n\n";
    out << ".tran 1u 10m\n";
    out << ".end\n";
    out.flush();

    // Run conversion
    QGraphicsScene scene;
    NetlistToSchematic::ConvertResult result = NetlistToSchematic::convertToScene(tempFile.fileName(), &scene);
    
    QVERIFY(result.success);
    QCOMPARE(result.componentCount, 3); // V1, R1, C1
    
    // GND node 0 is mapped to visual GND symbols.
    // V1 and C1 are connected to ground, which creates 2 GND symbols and 2 air wires.
    QVERIFY(result.airWireCount > 0);
    
    tempFile.close();
}

void TestSerializationAndCloning::testNetlistToSchematic_Robustness_SkippedComponents() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    
    QTextStream out(&tempFile);
    out << "* Netlist with invalid syntax and unrecognized symbols\n";
    out << "R1 net1 net2 1k\n";
    out << "INVALID_LINE_THAT_SHOULD_BE_SKIPPED\n";
    out << "U1 net2 net3 INVALID_COMP_TYPE\n"; // unrecognized prefix 'U', fallback to IC
    out << "X1 net3 0 MY_SUBCKT\n"; // subcircuit with unrecognized definition X1
    out << ".end\n";
    out.flush();

    QGraphicsScene scene;
    NetlistToSchematic::ConvertResult result = NetlistToSchematic::convertToScene(tempFile.fileName(), &scene);
    
    QVERIFY(result.success);
    // Should place R1, U1 (fallback to IC), and X1 (fallback to IC)
    QVERIFY(result.componentCount >= 2);
    
    tempFile.close();
}

void TestSerializationAndCloning::testNetlistToSchematic_DirectivesAndModels() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    
    QTextStream out(&tempFile);
    out << "* Netlist with .model mappings\n";
    out << ".model 2N3904 NPN\n";
    out << ".model 2N3906 PNP\n";
    out << ".model MyNmos NMOS\n";
    out << "Q1 c b e 2N3904\n";
    out << "Q2 c b e 2N3906\n";
    out << "M1 d g s s MyNmos\n";
    out << ".end\n";
    out.flush();

    // Parse the netlist using the parser directly to verify it captures model info
    SpiceNetlistParser::ParsedNetlist parsed = SpiceNetlistParser::parse(tempFile.fileName());
    QCOMPARE(parsed.components.size(), 3);
    
    // Q1 should map to Transistor (NPN)
    QCOMPARE(parsed.components[0].typeName, QString("Transistor"));
    // Q2 should map to Transistor_PNP
    QCOMPARE(parsed.components[1].typeName, QString("Transistor_PNP"));
    // M1 should map to Transistor_NMOS
    QCOMPARE(parsed.components[2].typeName, QString("Transistor_NMOS"));

    tempFile.close();
}

QTEST_MAIN(TestSerializationAndCloning)
