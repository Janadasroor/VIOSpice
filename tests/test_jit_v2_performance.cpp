/*
 * Copyright 2026 Janada Sroor
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <QtTest/QtTest>
#include <QObject>
#include "../core/simulation/jit_context_manager.h"
#include "../core/simulation/jit_bridge.h"
#include "../core/simulation/simulation_manager.h"
#include <iostream>
#include <chrono>

using namespace Flux;

class TestJitV2Performance : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
private slots:
    void testJacobianTrampolineAccuracy();
    void testPerformanceComparison();
};

void TestJitV2Performance::testJacobianTrampolineAccuracy() {
    JITContextManager& inst = JITContextManager::instance();
    QString scriptId = "test_accel";
    QString source = "def update(t, inputs) { return inputs[0] * inputs[0]; }";
    QMap<int, QString> errors;
    inst.setInputPinMapping(scriptId, {"In1"});
    inst.compileAndLoad(scriptId, source, errors);
    double inputs[1] = { 5.0 };
    double output = 0.0;
    double jacobian[1] = { 0.0 };
    JITContextManager::flux_jit_v2_jacobian_trampoline(scriptId.toUtf8().constData(), 0.0, inputs, &output, jacobian);
    QCOMPARE(output, 25.0);
    QVERIFY(std::abs(jacobian[0] - 10.0) < 1e-3);
}
void TestJitV2Performance::testPerformanceComparison() {
    JITContextManager& inst = JITContextManager::instance();
    QString scriptId = "perf_bench";
    QString source = "def update(t, inputs) { return sin(inputs[0]) + inputs[1] * 0.5; }";
    inst.setInputPinMapping(scriptId, {"In1", "In2"});
    QMap<int, QString> errors;
    inst.compileAndLoad(scriptId, source, errors);
    double inputs[2] = { 1.0, 2.0 };
    double output = 0.0;
    double jacobian[2] = { 0.0 };
    const int iterations = 100000;
    auto startV1 = std::chrono::high_resolution_clock::now();
    for(int i=0; i<iterations; ++i) {
        std::vector<double> inVec = {inputs[0], inputs[1]};
        inst.runUpdate(scriptId, 0.1, inVec);
    }
    auto endV1 = std::chrono::high_resolution_clock::now();
    auto startV2 = std::chrono::high_resolution_clock::now();
    for(int i=0; i<iterations; ++i) {
        JITContextManager::flux_jit_v2_jacobian_trampoline(scriptId.toUtf8().constData(), 0.1, inputs, &output, jacobian);
    }
    auto endV2 = std::chrono::high_resolution_clock::now();
    auto durV1 = std::chrono::duration_cast<std::chrono::microseconds>(endV1 - startV1).count();
    auto durV2 = std::chrono::duration_cast<std::chrono::microseconds>(endV2 - startV2).count();
    std::cout << "\n--- JIT Performance Benchmark ---" << std::endl;
    std::cout << "V1 (Legacy/Interpreted Path): " << durV1 << " us" << std::endl;
    std::cout << "V2 (Native/Jacobian Path):    " << durV2 << " us" << std::endl;
    std::cout << "Calculated Speedup:           " << (double)durV1 / (durV2 > 0 ? durV2 : 1) << "x" << std::endl;
    QVERIFY(durV2 < durV1);
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    TestJitV2Performance tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_jit_v2_performance.moc"
