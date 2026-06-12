# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

# JIT-Compiled Digital PWM Controller
# This FluxScript implements a basic constant-frequency PWM generator
# that can be used as a "Smart Signal" block in a SPICE simulation.

def update(t, inputs) {
    # Inputs:
    # inputs[0] - Control voltage (e.g. from an Error Amp or target)
    # inputs[1] - Ramp oscillator / feedback (optional)
    
    period = 1.0 / 100000.0  # 100kHz switching frequency
    
    # Calculate phase in current cycle (0.0 to 1.0)
    phase = (t / period) - floor(t / period)
    
    # Duty cycle controlled by input[0] (scaled 0-5V -> 0-100%)
    duty = inputs[0] / 5.0
    if (duty > 0.95) { duty = 0.95 }
    if (duty < 0.05) { duty = 0.05 }
    
    # PWM Output logic
    if (phase < duty) {
        return 5.0 # HIGH
    } else {
        return 0.0 # LOW
    }
}
