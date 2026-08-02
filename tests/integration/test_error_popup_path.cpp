/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 *
 * Verifies that a runtime ngspice failure reaches errorOccurred so the GUI
 * popup fires. Guards the regression where handleSimulationFinished only
 * reported m_lastRunFailed and silently dropped errors classified as
 * m_lastLoadFailed (any "Error:"/"fatal" text without the run-fail keywords).
 */

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSignalSpy>
#include <QTimer>

#include "simulation_manager.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    auto& sim = SimulationManager::instance();
    if (!sim.isAvailable()) {
        qCritical() << "Ngspice not available (HAVE_NGSPICE not defined)";
        return 1;
    }

    sim.initialize();

    // Failing netlist: hard convergence failure, unrecoverable.
    const char* failingNetlist =
        "Hard failure test\n"
        "V1 1 0 10\n"
        "B1 1 0 V=exp(v(1))\n"
        ".tran 1u 100m\n"
        ".options itl1=1 itl2=1 itl4=1\n"
        ".end\n";

    // Write to a .cir file so the raw path logic treats it as a file.
    QString path = QDir::tempPath() + "/fail_popup_test.cir";
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(failingNetlist);
    }

    QSignalSpy errorSpy(&sim, &SimulationManager::errorOccurred);
    QSignalSpy finishedSpy(&sim, qOverload<>(&SimulationManager::simulationFinished));

    sim.runSimulation(path, nullptr);

    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    QObject::connect(&sim, qOverload<>(&SimulationManager::simulationFinished), &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(15000);
    loop.exec();

    fprintf(stderr, "RESULT errorOccurred=%lld finished=%lld\n", (long long)errorSpy.count(), (long long)finishedSpy.count());
    for (int i = 0; i < errorSpy.count(); ++i) {
        fprintf(stderr, "RESULT error[%d]=%s\n", i, errorSpy.at(i).at(0).toString().toUtf8().constData());
    }

    bool ok = errorSpy.count() > 0;
    QFile::remove(path);
    return ok ? 0 : 2;
}
