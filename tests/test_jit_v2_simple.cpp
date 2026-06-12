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

#include "test_jit_v2_simple.h"
#include "../core/simulation/jit_context_manager.h"

using namespace Flux;

void TestJitV2Simple::testJacobianAccuracy() {
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

QTEST_GUILESS_MAIN(TestJitV2Simple)
