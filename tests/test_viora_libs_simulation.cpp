/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_viora_libs_simulation.h"
#include "../symbols/symbol_library.h"
#include "../footprints/footprint_library.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDebug>

static bool runVioraNetlist(const QString& cirPath) {
    QString appPath = QCoreApplication::applicationDirPath() + "/viora";
#ifdef _WIN32
    if (!QFile::exists(appPath)) {
        appPath += ".exe";
    }
#endif
    if (!QFile::exists(appPath)) {
        appPath = "viora"; // Fallback to PATH
    }

    QProcess proc;
    proc.start(appPath, QStringList() << "netlist-run" << cirPath << "--json");
    if (!proc.waitForFinished(60000)) {
        return false;
    }
    return (proc.exitCode() == 0);
}

void TestVioraLibsSimulation::initTestCase() {
    qputenv("VIORA_NO_DAEMON", "1");
    // Determine path to viora-libs or ViospiceLib
    QString home = QDir::homePath();
    QString candidate1 = home + "/viora-libs";
    QString candidate2 = home + "/ViospiceLib";

    if (QDir(candidate1).exists()) {
        m_libsPath = candidate1;
    } else if (QDir(candidate2).exists()) {
        m_libsPath = candidate2;
    } else {
        m_libsPath = candidate1; // Will clone here
    }
}

void TestVioraLibsSimulation::testLibraryRepositoryDiscoveryAndFetch() {
    QDir libsDir(m_libsPath);
    if (!libsDir.exists() || !QFile::exists(m_libsPath + "/sym")) {
        qDebug() << "viora-libs repository not found locally. Cloning from GitHub...";
        QProcess gitProc;
        gitProc.start("git", QStringList() << "clone" << "https://github.com/Janadasroor/viora-libs.git" << m_libsPath);
        QVERIFY2(gitProc.waitForFinished(180000), "Git clone of viora-libs repository timed out");
        QCOMPARE(gitProc.exitCode(), 0);
    }

    QVERIFY2(QDir(m_libsPath + "/sym").exists(), "sym/ directory must exist in viora-libs");
    QVERIFY2(QDir(m_libsPath + "/footprints").exists(), "footprints/ directory must exist in viora-libs");
    QVERIFY2(QDir(m_libsPath + "/models").exists(), "models/ directory must exist in viora-libs");
}

void TestVioraLibsSimulation::testSymbolLoadingFromLibsRepo() {
    QString symDir = m_libsPath + "/sym";
    QVERIFY(QDir(symDir).exists());

    SymbolLibraryManager::instance().loadUserLibraries(symDir, false);
    auto libs = SymbolLibraryManager::instance().libraries();
    qDebug() << "Loaded" << libs.size() << "symbol libraries from" << symDir;
    QVERIFY2(libs.size() > 0, "Symbol library manager should contain loaded libraries");
}

void TestVioraLibsSimulation::testFootprintLoadingFromLibsRepo() {
    QString footDir = m_libsPath + "/footprints";
    QVERIFY(QDir(footDir).exists());

    FootprintLibraryManager::instance().loadUserLibraries(footDir);
    auto libs = FootprintLibraryManager::instance().libraries();
    qDebug() << "Loaded" << libs.size() << "footprint libraries from" << footDir;
    QVERIFY2(libs.size() > 0, "Footprint library manager should contain loaded libraries");
}

void TestVioraLibsSimulation::testOpAmpModelSimulationPipeline() {
    QString cirPath = QDir::tempPath() + "/test_opamp_ci.cir";
    QString rawPath = QDir::tempPath() + "/test_opamp_ci.raw";

    QFile file(cirPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "* OpAmp Simulation Test\n"
            << ".subckt TL072 1 2 3 4 5\n"
            << "E1 5 0 1 2 100k\n"
            << ".ends TL072\n"
            << "VCC VCC 0 DC 12\n"
            << "VEE VEE 0 DC -12\n"
            << "VIN IN 0 SIN(0 0.5 100)\n"
            << "X1 IN VFB VCC VEE OUT TL072\n"
            << "R1 VFB 0 1k\n"
            << "R2 OUT VFB 4k\n"
            << ".tran 0.1m 10m\n"
            << ".end\n";
        file.close();
    }

    bool ok = runVioraNetlist(cirPath);
    QVERIFY2(ok, "OpAmp SPICE simulation execution failed");
    QVERIFY2(QFile::exists(rawPath), "Raw simulation output file should be generated");
}

void TestVioraLibsSimulation::testDiodeModelSimulationPipeline() {
    QString cirPath = QDir::tempPath() + "/test_diode_ci.cir";
    QString rawPath = QDir::tempPath() + "/test_diode_ci.raw";

    QFile file(cirPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "* Diode Simulation Test\n"
            << "VIN IN 0 SIN(0 10 50)\n"
            << "D1 IN OUT D1N4148\n"
            << "RL OUT 0 1k\n"
            << ".model D1N4148 D(IS=2.52n RS=0.568 N=1.752)\n"
            << ".tran 0.2m 20m\n"
            << ".end\n";
        file.close();
    }

    bool ok = runVioraNetlist(cirPath);
    QVERIFY2(ok, "Diode SPICE simulation execution failed");
    QVERIFY2(QFile::exists(rawPath), "Raw simulation output file should be generated");
}

void TestVioraLibsSimulation::testTransistorModelSimulationPipeline() {
    QString cirPath = QDir::tempPath() + "/test_bjt_ci.cir";
    QString rawPath = QDir::tempPath() + "/test_bjt_ci.raw";

    QFile file(cirPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "* BJT Simulation Test\n"
            << "VIN IN 0 SIN(0 20m 1k)\n"
            << "VCC VCC 0 DC 12\n"
            << "C1 IN BASE 10u\n"
            << "R1 VCC BASE 47k\n"
            << "R2 BASE 0 10k\n"
            << "RC VCC COLL 2.2k\n"
            << "RE EMIT 0 1k\n"
            << "Q1 COLL BASE EMIT Q2N2222\n"
            << ".model Q2N2222 NPN(IS=14.34f BF=255.9)\n"
            << ".tran 10u 2m\n"
            << ".end\n";
        file.close();
    }

    bool ok = runVioraNetlist(cirPath);
    QVERIFY2(ok, "BJT SPICE simulation execution failed");
    QVERIFY2(QFile::exists(rawPath), "Raw simulation output file should be generated");
}

void TestVioraLibsSimulation::cleanupTestCase() {
    QFile::remove(QDir::tempPath() + "/test_opamp_ci.cir");
    QFile::remove(QDir::tempPath() + "/test_opamp_ci.raw");
    QFile::remove(QDir::tempPath() + "/test_diode_ci.cir");
    QFile::remove(QDir::tempPath() + "/test_diode_ci.raw");
    QFile::remove(QDir::tempPath() + "/test_bjt_ci.cir");
    QFile::remove(QDir::tempPath() + "/test_bjt_ci.raw");
}

QTEST_MAIN(TestVioraLibsSimulation)
