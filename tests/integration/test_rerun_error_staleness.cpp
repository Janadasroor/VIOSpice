/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reproduces stale-error bug: run a failing netlist, then run a valid one.
 * The valid run must NOT emit errorOccurred, and must produce results.
 */

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSignalSpy>
#include <QTimer>

#include "simulation_manager.h"

static int runAndWait(SimulationManager& sim, const QString& path,
                      QSignalSpy* errorSpy, QSignalSpy* rawSpy, const QString& label) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&sim, qOverload<>(&SimulationManager::simulationFinished), &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(20000);

    const int errsBefore = errorSpy->count();
    const int rawsBefore = rawSpy->count();

    sim.runSimulation(path, nullptr);
    loop.exec();

    const int errs = errorSpy->count() - errsBefore;
    const int raws = rawSpy->count() - rawsBefore;
    fprintf(stderr, "RUN[%s]: newErrors=%d newRawResults=%d\n", qPrintable(label), errs, raws);
    return errs;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    auto& sim = SimulationManager::instance();
    if (!sim.isAvailable()) {
        fprintf(stderr, "Ngspice not available\n");
        return 1;
    }
    sim.initialize();

    const QString failPath = QDir::tempPath() + "/rerun_fail.cir";
    const QString validPath = QDir::tempPath() + "/rerun_valid.cir";
    {
        QFile f(failPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("Hard failure test\nV1 1 0 10\nB1 1 0 V=exp(v(1))\n.tran 1u 10m\n.options itl1=1 itl2=1 itl4=1\n.end\n");
            f.close();
        }
    }
    {
        QFile f(validPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("Valid RC test\nV1 1 0 10\nR1 1 2 1k\nC1 2 0 1u\n.tran 1u 1m\n.end\n");
            f.close();
        }
    }

    QSignalSpy errorSpy(&sim, &SimulationManager::errorOccurred);
    QSignalSpy rawSpy(&sim, &SimulationManager::rawResultsReady);

    const int failErrs = runAndWait(sim, failPath, &errorSpy, &rawSpy, "FAIL");
    const int validErrs = runAndWait(sim, validPath, &errorSpy, &rawSpy, "VALID-after-fail");
    const int validErrs2 = runAndWait(sim, validPath, &errorSpy, &rawSpy, "VALID-again");

    QFile::remove(failPath);
    QFile::remove(validPath);

    bool ok = (failErrs > 0) && (validErrs == 0) && (validErrs2 == 0);
    fprintf(stderr, "RESULT ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 2;
}
