/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reproduces live-mode re-run bug: start a shared run, stop it mid-run,
 * then start again. The second start must actually begin (simulationStarted)
 * and must not emit errorOccurred.
 */

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>

#include "../../simulator/bridge/sim_manager.h"
#include "../../core/simulation/simulation_manager.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    auto& sim = SimManager::instance();
    SimulationManager::instance().initialize();

    const QString validNetlist =
        "Long RC test\n"
        "V1 1 0 10\n"
        "R1 1 2 1k\n"
        "C1 2 0 1u\n"
        ".tran 1u 2m\n"
        ".end\n";

    QSignalSpy errorSpy(&sim, &SimManager::errorOccurred);
    QSignalSpy startedSpy(&sim, &SimManager::simulationStarted);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStop = 0.002;
    config.tStep = 1e-6;
    sim.runNgspiceSimulation(validNetlist, config);

    // Let it run briefly, then stop it mid-run like a live-mode re-run would.
    QEventLoop loop;
    QTimer::singleShot(300, &loop, &QEventLoop::quit);
    loop.exec();

    const int startedBefore = startedSpy.count();
    sim.stopAll();
    // Mimic SimulationPanel::onRunSimulation retry loop: poll isRunning() until
    // the engine has fully shut down (async cleanup), then start the new run.
    int attempts = 20;
    while (sim.isRunning() && attempts-- > 0) {
        QEventLoop tick;
        QTimer::singleShot(75, &tick, &QEventLoop::quit);
        tick.exec();
    }
    fprintf(stderr, "RE-RUN: attemptsLeft=%d isRunning()=%d\n", attempts, sim.isRunning() ? 1 : 0);

    sim.runNgspiceSimulation(validNetlist, config);

    QEventLoop loop3;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&sim, &SimManager::simulationFinished, &loop3, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop3, &QEventLoop::quit);
    timer.start(20000);
    loop3.exec();

    const int errs = errorSpy.count();
    const int starts = startedSpy.count() - startedBefore;
    fprintf(stderr, "RE-RUN: newStarts=%d totalErrors=%d\n", starts, errs);
    for (int i = 0; i < errs; ++i) {
        fprintf(stderr, "RE-RUN error[%d]=%s\n", i, errorSpy.at(i).at(0).toString().toUtf8().constData());
    }

    bool ok = (starts >= 1) && (errs == 0);
    fprintf(stderr, "RESULT ok=%d\n", ok ? 1 : 0);
    sim.stopAll();
    app.processEvents();
    return ok ? 0 : 2;
}
