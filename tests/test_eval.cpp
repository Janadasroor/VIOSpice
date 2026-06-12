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

#include <iostream>
#include <vector>
#include "core/simulation/jit_context_manager.h"
#include <QMap>
#include <QString>

using namespace Flux;

int main() {
    JITContextManager& inst = JITContextManager::instance();
    QString scriptId = "test_diff";
    QString source = R"(
# Signal Differencer Template
# INPUTS: sig_a, sig_b
# OUTPUTS: diff

let a = inputs[0] in
let b = inputs[1] in
a - b
)";

    QMap<int, QString> errors;
    inst.setInputPinMapping(scriptId, {"sig_a", "sig_b"});
    bool ok = inst.compileAndLoad(scriptId, source, errors);
    if (!ok) {
        std::cerr << "Compile failed: " << errors[0].toStdString() << "\n";
        return 1;
    }
    
    double inputs[2] = { 10.0, 3.0 };
    double output = 0.0;
    JITContextManager::flux_jit_v2_jacobian_trampoline(scriptId.toUtf8().constData(), 0.0, inputs, &output, nullptr);
    
    std::cout << "Output: " << output << "\n";
    return 0;
}
