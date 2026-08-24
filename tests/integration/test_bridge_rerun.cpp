/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reproduces stale-error bug at the GUI bridge layer (SimManager).
 * Run a failing netlist, then a valid one, via SimManager::runNgspiceSimulation.
 */

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>

#include "../../simulator/bridge/sim_manager.h"
#include "../../core/simulation/simulation_manager.h"

static int runAndWait(SimManager& sim, const QString& netlist, const QString& label,
                      QSignalSpy* errorSpy, QSignalSpy* finishedSpy) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&sim, &SimManager::simulationFinished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(20000);

    const int errsBefore = errorSpy->count();
    const int finBefore = finishedSpy->count();

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStop = 1e-3;
    config.tStep = 1e-6;
    sim.runNgspiceSimulation(netlist, config);

    loop.exec();

    const int errs = errorSpy->count() - errsBefore;
    const int fins = finishedSpy->count() - finBefore;
    fprintf(stderr, "BRIDGE[%s]: newErrors=%d newFinished=%d\n", qPrintable(label), errs, fins);
    for (int i = errsBefore; i < errorSpy->count(); ++i) {
        fprintf(stderr, "BRIDGE[%s] error[%d]=%s\n", qPrintable(label), i - errsBefore,
                errorSpy->at(i).at(0).toString().toUtf8().constData());
    }
    return errs;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    auto& sim = SimManager::instance();
    SimulationManager::instance().initialize();

    const QString failNetlist =
        "Hard failure test\n"
        "V1 1 0 10\n"
        "B1 1 0 V=exp(v(1))\n"
        ".tran 1u 100m\n"
        ".options itl1=1 itl2=1 itl4=1\n"
        ".end\n";

    const QString validNetlist =
        "Valid RC test\n"
        "V1 1 0 10\n"
        "R1 1 2 1k\n"
        "C1 2 0 1u\n"
        ".tran 1u 10m\n"
        ".end\n";

    QSignalSpy errorSpy(&sim, &SimManager::errorOccurred);
    QSignalSpy finishedSpy(&sim, &SimManager::simulationFinished);

    const int failErrs = runAndWait(sim, failNetlist, "FAIL", &errorSpy, &finishedSpy);
    const int validErrs = runAndWait(sim, validNetlist, "VALID-after-fail", &errorSpy, &finishedSpy);
    const int validErrs2 = runAndWait(sim, validNetlist, "VALID-again", &errorSpy, &finishedSpy);

    bool ok = (failErrs > 0) && (validErrs == 0) && (validErrs2 == 0);
    fprintf(stderr, "RESULT ok=%d\n", ok ? 1 : 0);
    return ok ? 0 : 2;
}
