/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_UX_LOGIC_H
#define TEST_UX_LOGIC_H

#include <QObject>
#include <QTest>

class TestUXLogic : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testMenuRegistry_SingleSelection();
    void testMenuRegistry_MixedSelection();
    void testEngineeringNotationRegex();
    void testDoubleClickRouting_PrioritizesLabels();
    void testSelectDrag_ConnectedWireChainRemainsAttachedRealtime();
    void testSelectDrag_ResistorKeepsNearSegmentJunctionAttached();
    void testSelect_ClickOnComponentPrimitiveEdgeSelectsOwner();
    void testSelect_ClickOnConnectedResistorPinPrefersComponentCapture();
    void testSelect_ProbeClickOnComponentBodyEmitsCurrentWaveform();
    void testSelect_DragTransistorKeepsTJunctionBranchesAttached();
    void testSelectDrag_SequentialMovesKeepWireEndpointsAttached();
    void testSelectDrag_HorizontalMovePrefersHorizontalFirstElbow();
    void testConnectivity_JunctionDotsFollowRules();
    void testSelectDrag_AnchoredWireBetweenComponentsStaysAttached();
    void testSelectDrag_RebuildsSimpleLWireBetweenComponents();
    void testSelectDrag_WirePointsRemainGridAlignedAfterMoves();
    void testSelectDrag_MultiSegmentWirePreservesPointCount();
    void testSelectDrag_LWireAvoidsObstacleByFlippingElbow();
    void testSelectDrag_LWireNudgesWhenBothPathsBlocked();
    void testSelectDrag_CollinearChainMovesWithEndpoint();
    void testTransistor_PinsAreGridAligned();
    void testLtspiceAscImport_BasicFixture();
    void testLtspiceAscImport_ShapeAndPortTokens();
    void testSmartSignal_DefaultFluxCodeCompilesCleanly();
    void testSmartSignal_BareUpdateAndDefSignatures();
    void testSmartSignal_PinVoltageMappingAndCaseInsensitivity();
    void testSmartSignal_LogicEditorPanel_InstantiationAndPreview();
};

#endif // TEST_UX_LOGIC_H
