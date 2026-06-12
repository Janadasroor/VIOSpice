# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

# PID Controller Template (Clean)
# INPUTS: setpoint, feedback
# OUTPUTS: out

error = setpoint - feedback
Kp = 1.5
error * Kp
