/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include <QCoreApplication>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <iostream>
#include <string>

#ifndef VIO_CMD_PATH
#define VIO_CMD_PATH "viora"
#endif

int runNetlist(const QString &cirPath, const QString &analysis) {
    QProcess proc;
    proc.start(QString::fromUtf8(VIO_CMD_PATH),
               {"netlist-run", cirPath, "--analysis", analysis});
    if (!proc.waitForFinished(30000)) {
        proc.kill();
        return -1;
    }
    return proc.exitCode();
}

int main(int argc, char *argv[]) {
    qputenv("VIORA_NO_DAEMON", "1");
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::cerr << "Usage: test_sim_golden <circuits_dir>" << std::endl;
        return 1;
    }

    const QString circuitsDir = QString::fromUtf8(argv[1]);
    int passed = 0, failed = 0;

    auto runTest = [&](const QString &file, const QString &analysis) {
        QString path = circuitsDir + "/" + file;
        if (!QFile::exists(path)) {
            std::cout << "SKIP: " << file.toStdString() << " (not found)" << std::endl;
            return;
        }
        int rc = runNetlist(path, analysis);
        if (rc == 0) {
            std::cout << "PASS: " << file.toStdString() << std::endl;
            ++passed;
        } else {
            std::cerr << "FAIL: " << file.toStdString()
                      << " (exit=" << rc << ")" << std::endl;
            ++failed;
        }
    };

    // OP analysis
    runTest("test_op.cir", "op");
    runTest("test_dc.cir", "op");
    runTest("test_simple.cir", "op");
    runTest("test_gm.cir", "op");
    runTest("test_op2.cir", "op");

    // Transient analysis
    runTest("rc_circuit.cir", "tran");

    // AC analysis
    runTest("test_a_simple.cir", "ac");
    runTest("test_a_devices.cir", "ac");

    std::cout << "\n=== Golden-trace results: " << passed << " passed, "
              << failed << " failed ===" << std::endl;

    return failed > 0 ? 1 : 0;
}
