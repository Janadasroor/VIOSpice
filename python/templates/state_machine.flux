# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

# Logic Comparator Template
# INPUTS: sig, ref
# OUTPUTS: out

let s = inputs[0] in
let r = inputs[1] in

if s > r then 1.0 else 0.0
