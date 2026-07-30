/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_VIORA_LIBS_SIMULATION_H
#define TEST_VIORA_LIBS_SIMULATION_H

#include <QObject>
#include <QTest>

class TestVioraLibsSimulation : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testLibraryRepositoryDiscoveryAndFetch();
    void testSymbolLoadingFromLibsRepo();
    void testFootprintLoadingFromLibsRepo();
    void testOpAmpModelSimulationPipeline();
    void testDiodeModelSimulationPipeline();
    void testTransistorModelSimulationPipeline();
    void cleanupTestCase();

private:
    QString m_libsPath;
};

#endif // TEST_VIORA_LIBS_SIMULATION_H
