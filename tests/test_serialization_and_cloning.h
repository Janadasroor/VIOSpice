/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_SERIALIZATION_AND_CLONING_H
#define TEST_SERIALIZATION_AND_CLONING_H

#include <QObject>
#include <QTest>
#include "../footprints/models/footprint_primitive.h"
#include "../footprints/models/footprint_definition.h"
#include "../pcb/models/trace_model.h"
#include "../pcb/models/via_model.h"
#include "../pcb/models/pad_model.h"
#include "../pcb/models/component_model.h"
#include "../pcb/models/copper_pour_model.h"
#include "../schematic/models/schematic_component_model.h"
#include "../schematic/items/wire_item.h"
#include "../schematic/io/netlist_to_schematic.h"
#include "../schematic/io/spice_netlist_parser.h"

class TestSerializationAndCloning : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // Footprints serialization tests (Phase 1 & 2)
    void testFootprintPrimitiveSerialization_StringNames();
    void testFootprintPrimitiveDeserialization_LegacyFallback();
    void testFootprintSchemaConstants();

    // PCB elements cloning tests (Phase 3 & 4)
    void testTraceModelCloning();
    void testViaModelCloning();
    void testPadModelCloning();
    void testComponentModelCloning();
    void testCopperPourModelCloning();

    // Schematic elements cloning & serialization tests (Phase 5)
    void testSchematicComponentModelCloning();
    void testWireItemSerialization_StringWireType();
    void testSchematicSchemaConstants();

    // Netlist to Schematic converter stability tests
    void testNetlistToSchematic_Basic();
    void testNetlistToSchematic_Robustness_SkippedComponents();
    void testNetlistToSchematic_DirectivesAndModels();
};

#endif // TEST_SERIALIZATION_AND_CLONING_H
