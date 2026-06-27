# AVR Co-Simulation Tests

Tests for VioAVR co-simulation via ngspice `d_cosim` XSPICE code model.

## Prerequisites

- VioAVR built: `build/libavr_cosim.so`
- avr-gcc installed
- viora CLI or VioSpice app

## Running

```bash
# From viospice root
cd tests/circuits/avr

# Run any test
viora netlist-run blink_test5.cir --analysis tran --stop 1ms --timeout 120s

# Or with the GUI
# Open .cir files directly in VioraEDA
```

## Tests

| File | Firmware | What it tests |
|------|----------|---------------|
| `blink_test5.cir` | `blink_fast.c` | Digital output toggle (PB5 @ 100us) |
| `pwm_dac_test.cir` | `pwm_dac.c` | PWM generation + RC filter analog |
| `adc_threshold_test.cir` | `adc_threshold.c` | ADC input → digital output threshold |
| `timer_isr_test.cir` | `timer_isr.c` | Timer1 CTC interrupt (1 Hz blink) |

## Pin Mapping (ATmega328P)

```
PORTA = ext_id 0-7
PORTB = ext_id 8-15  (PB5 = ext_id 13)
PORTC = ext_id 16-23
PORTD = ext_id 24-31
```

## Key Patterns

```spice
* d_cosim: 32 d_in + 32 d_out per chip
A_AVR [d_in...] [d_out...] d_cosim_model
.model d_cosim_model d_cosim(simulation="libavr_cosim.so" sim_args=["mcu","hex.hex"] queue_size=1024 irreversible=1 delay=1e-9)

* dac_bridge: digital -> analog for measurement
A_dac [pb5_dig] [pb5_an 0] dac_bridge_model
.model dac_bridge_model dac_bridge(out_low=0.0 out_high=5.0 input_load=1e12)

* avr_adc_bridge: analog -> AVR ADC channel
A_avr_adc adc_in dummy avr_adc_bridge_model
.model avr_adc_bridge_model avr_adc_bridge(channel=0)
```
