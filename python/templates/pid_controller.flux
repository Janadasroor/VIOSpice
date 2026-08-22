# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

# PID Controller Template (Clean)
# INPUTS: setpoint, feedback
# OUTPUTS: out

let setpoint = inputs[0] in
let feedback = inputs[1] in
let error = setpoint - feedback in
let Kp = 1.5 in
return error * Kp;


