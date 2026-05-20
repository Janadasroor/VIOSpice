#include "xspice_block_item.h"
#include <QPainter>
#include <QFontMetrics>
#include <QGraphicsScene>
#include <QStyleOptionGraphicsItem>
#include <QJsonDocument>

// ─── Model Database ──────────────────────────────────────────────────────────

static QVector<XspiceModelDef> buildModelDB() {
    QVector<XspiceModelDef> db;

    // ── Analog Blocks ────────────────────────────────────────────────────────

    auto addAnalog = [&](const QString& name, const QString& spiceType, const QString& desc) {
        XspiceModelDef def;
        def.name = name;
        def.category = "Analog Behavioral";
        def.spiceType = spiceType;
        def.description = desc;
        def.inputPinCount = 1;
        def.outputPinCount = 1;
        def.pins = {{"IN", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        return def;
    };

    auto addParam = [](XspiceModelDef& def, const QString& n, const QVariant& d, const QString& desc,
                       XspiceParamDef::Widget w = XspiceParamDef::SpinboxDouble,
                       double min = -1e12, double max = 1e12) {
        XspiceParamDef p;
        p.name = n; p.defaultValue = d; p.description = desc; p.widget = w; p.min = min; p.max = max;
        def.params.append(p);
    };

    // gain
    {
        auto d = addAnalog("gain", "gain", "Simple gain block: V(out) = gain * V(in)");
        addParam(d, "gain", 1.0, "Gain factor");
        addParam(d, "in_offset", 0.0, "Input offset voltage");
        addParam(d, "out_offset", 0.0, "Output offset voltage");
        db.append(d);
    }

    // summer (vector input)
    {
        XspiceModelDef d;
        d.name = "summer"; d.category = "Analog Behavioral"; d.spiceType = "summer";
        d.description = "Summing amplifier: V(out) = sum(gain[i] * V(in[i]))";
        d.inputPinCount = 2; d.outputPinCount = 1;
        d.pins = {{"IN", XspicePinDef::VoltageIn, 2, true}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "out_gain", 1.0, "Output gain");
        addParam(d, "out_offset", 0.0, "Output offset");
        db.append(d);
    }

    // mult
    {
        auto d = addAnalog("mult", "mult", "Multiplier: V(out) = product(V(in[i]))");
        d.inputPinCount = 2;
        d.pins = {{"IN", XspicePinDef::VoltageIn, 2, true}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "out_gain", 1.0, "Output gain");
        addParam(d, "out_offset", 0.0, "Output offset");
        db.append(d);
    }

    // divide
    {
        XspiceModelDef d;
        d.name = "divide"; d.category = "Analog Behavioral"; d.spiceType = "divide";
        d.description = "Divider: V(out) = gain * num / den";
        d.inputPinCount = 2; d.outputPinCount = 1;
        d.pins = {{"NUM", XspicePinDef::VoltageIn}, {"DEN", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "num_gain", 1.0, "Numerator gain");
        addParam(d, "den_gain", 1.0, "Denominator gain");
        addParam(d, "den_lower_limit", 1e-10, "Minimum denominator", XspiceParamDef::SpinboxDouble, 1e-20, 1);
        db.append(d);
    }

    // int (integrator)
    {
        auto d = addAnalog("integrator", "int", "Integrator: V(out) = integral(gain * V(in)) + IC");
        addParam(d, "gain", 1.0, "Integrator gain");
        addParam(d, "out_ic", 0.0, "Initial condition (V)");
        addParam(d, "out_lower_limit", QVariant(), "Output lower limit (empty = none)");
        addParam(d, "out_upper_limit", QVariant(), "Output upper limit (empty = none)");
        db.append(d);
    }

    // d_dt (differentiator)
    {
        auto d = addAnalog("differentiator", "d_dt", "Differentiator: V(out) = gain * dV(in)/dt");
        addParam(d, "gain", 1.0, "Derivative gain");
        addParam(d, "out_offset", 0.0, "Output offset");
        db.append(d);
    }

    // limit
    {
        auto d = addAnalog("limiter", "limit", "Limiter: limits output between bounds with smooth transition");
        addParam(d, "gain", 1.0, "Gain");
        addParam(d, "out_lower_limit", -10.0, "Output lower limit");
        addParam(d, "out_upper_limit", 10.0, "Output upper limit");
        addParam(d, "limit_range", 1e-6, "Transition smoothness");
        db.append(d);
    }

    // slew (slew rate limiter)
    {
        auto d = addAnalog("slew_limiter", "slew", "Slew-rate limiter: limits dV/dt of output");
        addParam(d, "rise_slope", 1e-6, "Maximum rising slope (V/s)");
        addParam(d, "fall_slope", 1e-6, "Maximum falling slope (V/s)");
        db.append(d);
    }

    // s_xfer (s-domain transfer function)
    {
        auto d = addAnalog("s_domain_filter", "s_xfer", "S-domain transfer function: H(s) = gain * N(s)/D(s)");
        addParam(d, "num_coeff", "1.0", "Numerator coefficients (space-separated)", XspiceParamDef::LineEdit);
        addParam(d, "den_coeff", "1.0 1.0", "Denominator coefficients (space-separated)", XspiceParamDef::LineEdit);
        addParam(d, "gain", 1.0, "Overall gain");
        addParam(d, "denormalized_freq", 1.0, "Denormalization frequency (rad/s)");
        db.append(d);
    }

    // hyst (hysteresis / Schmitt trigger)
    {
        auto d = addAnalog("hysteresis", "hyst", "Schmitt trigger / hysteresis comparator");
        addParam(d, "in_low", 0.0, "Input low threshold");
        addParam(d, "in_high", 1.0, "Input high threshold");
        addParam(d, "hyst", 0.1, "Hysteresis voltage");
        addParam(d, "out_lower_limit", 0.0, "Output low level");
        addParam(d, "out_upper_limit", 1.0, "Output high level");
        db.append(d);
    }

    // sine (VCO)
    {
        auto d = addAnalog("vco_sine", "sine", "Voltage-controlled sine oscillator");
        d.pins = {{"CTRL", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "freq_center", 1000.0, "Center frequency (Hz)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "freq_dev", 500.0, "Frequency deviation (Hz/V)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "out_low", -1.0, "Output low level");
        addParam(d, "out_high", 1.0, "Output high level");
        db.append(d);
    }

    // square (VCO)
    {
        auto d = addAnalog("vco_square", "square", "Voltage-controlled square wave oscillator");
        d.pins = {{"CTRL", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "freq_center", 1000.0, "Center frequency (Hz)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "freq_dev", 500.0, "Frequency deviation (Hz/V)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "duty_cycle", 0.5, "Duty cycle (0-1)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "out_low", -1.0, "Output low level");
        addParam(d, "out_high", 1.0, "Output high level");
        db.append(d);
    }

    // triangle (VCO)
    {
        auto d = addAnalog("vco_triangle", "triangle", "Voltage-controlled triangle wave oscillator");
        d.pins = {{"CTRL", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "freq_center", 1000.0, "Center frequency (Hz)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "freq_dev", 500.0, "Frequency deviation (Hz/V)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "out_low", -1.0, "Output low level");
        addParam(d, "out_high", 1.0, "Output high level");
        db.append(d);
    }

    // pwl (piecewise-linear transfer)
    {
        auto d = addAnalog("pwl_transfer", "pwl", "Piecewise-linear transfer function y = f(x)");
        addParam(d, "x_values", "0 1", "X breakpoints (space-separated)", XspiceParamDef::LineEdit);
        addParam(d, "y_values", "0 1", "Y breakpoints (space-separated)", XspiceParamDef::LineEdit);
        addParam(d, "input_domain", 0.01, "Input smoothing domain");
        db.append(d);
    }

    // delay (analog delay line)
    {
        auto d = addAnalog("delay_line", "delay", "Analog delay line with circular buffer");
        addParam(d, "delay", 1e-6, "Delay time (seconds)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "buffer_size", 1024, "Buffer size (samples)", XspiceParamDef::SpinboxInt, 16, 1e6);
        db.append(d);
    }

    // oneshot
    {
        XspiceModelDef d;
        d.name = "oneshot"; d.category = "Analog Behavioral"; d.spiceType = "oneshot";
        d.description = "Monostable multivibrator (one-shot pulse generator)";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"TRIG", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "pulse_width", 1e-6, "Output pulse width (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "out_low", 0.0, "Output low level");
        addParam(d, "out_high", 1.0, "Output high level");
        addParam(d, "pos_edge_trig", 1.0, "Positive edge trigger (1=yes, 0=no)", XspiceParamDef::Checkbox);
        addParam(d, "retrig", 0.0, "Retriggerable (1=yes, 0=no)", XspiceParamDef::Checkbox);
        db.append(d);
    }

    // ── Digital Logic ────────────────────────────────────────────────────────

    auto addDigital = [&](const QString& name, const QString& spiceType, const QString& desc,
                          int inputs, int outputs, const QStringList& pinNames) {
        XspiceModelDef d;
        d.name = name; d.category = "Digital Logic"; d.spiceType = spiceType;
        d.description = desc;
        d.inputPinCount = inputs; d.outputPinCount = outputs;
        for (int i = 0; i < inputs; ++i)
            d.pins.append({pinNames.value(i, QString("IN%1").arg(i+1)), XspicePinDef::Digital});
        for (int i = 0; i < outputs; ++i)
            d.pins.append({pinNames.value(inputs + i, QString("OUT%1").arg(i+1)), XspicePinDef::Digital});
        return d;
    };

    auto addDigitalParam = [&](XspiceModelDef& d) {
        addParam(d, "rise_delay", 1e-9, "Rise delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "fall_delay", 1e-9, "Fall delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
    };

    {
        auto d = addDigital("AND Gate", "d_and", "n-input AND gate", 2, 1, {"A", "B", "Y"});
        d.pins[0].isVector = true; d.pins[0].minCount = 2;
        addDigitalParam(d); db.append(d);
    }
    {
        auto d = addDigital("OR Gate", "d_or", "n-input OR gate", 2, 1, {"A", "B", "Y"});
        d.pins[0].isVector = true; d.pins[0].minCount = 2;
        addDigitalParam(d); db.append(d);
    }
    {
        auto d = addDigital("NAND Gate", "d_nand", "n-input NAND gate", 2, 1, {"A", "B", "Y"});
        d.pins[0].isVector = true; d.pins[0].minCount = 2;
        addDigitalParam(d); db.append(d);
    }
    {
        auto d = addDigital("NOR Gate", "d_nor", "n-input NOR gate", 2, 1, {"A", "B", "Y"});
        d.pins[0].isVector = true; d.pins[0].minCount = 2;
        addDigitalParam(d); db.append(d);
    }
    {
        auto d = addDigital("XOR Gate", "d_xor", "n-input XOR gate", 2, 1, {"A", "B", "Y"});
        d.pins[0].isVector = true; d.pins[0].minCount = 2;
        addDigitalParam(d); db.append(d);
    }
    {
        auto d = addDigital("XNOR Gate", "d_xnor", "n-input XNOR gate", 2, 1, {"A", "B", "Y"});
        d.pins[0].isVector = true; d.pins[0].minCount = 2;
        addDigitalParam(d); db.append(d);
    }
    {
        auto d = addDigital("Inverter", "d_inverter", "Digital inverter (NOT gate)", 1, 1, {"IN", "OUT"});
        addDigitalParam(d); db.append(d);
    }
    {
        auto d = addDigital("Buffer", "d_buffer", "Digital buffer", 1, 1, {"IN", "OUT"});
        addDigitalParam(d); db.append(d);
    }
    {
        auto d = addDigital("Tri-State Buffer", "d_tristate", "Tri-state buffer with enable", 2, 1, {"IN", "EN", "OUT"});
        addDigitalParam(d); db.append(d);
    }

    // D Flip-Flop
    {
        XspiceModelDef d;
        d.name = "D Flip-Flop"; d.category = "Digital Logic"; d.spiceType = "d_dff";
        d.description = "D-type flip-flop with async set/reset";
        d.inputPinCount = 3; d.outputPinCount = 2;
        d.pins = {{"D", XspicePinDef::Digital}, {"CLK", XspicePinDef::Digital},
                  {"RST", XspicePinDef::Digital, 0, false}, {"Q", XspicePinDef::Digital}, {"NQ", XspicePinDef::Digital, 0, false}};
        addDigitalParam(d);
        addParam(d, "clk_delay", 1e-9, "Clock delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "set_delay", 1e-9, "Set delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "reset_delay", 1e-9, "Reset delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        db.append(d);
    }

    // JK Flip-Flop
    {
        XspiceModelDef d;
        d.name = "JK Flip-Flop"; d.category = "Digital Logic"; d.spiceType = "d_jkff";
        d.description = "JK flip-flop with async set/reset";
        d.inputPinCount = 3; d.outputPinCount = 2;
        d.pins = {{"J", XspicePinDef::Digital}, {"K", XspicePinDef::Digital}, {"CLK", XspicePinDef::Digital},
                  {"Q", XspicePinDef::Digital}, {"NQ", XspicePinDef::Digital}};
        addDigitalParam(d); db.append(d);
    }

    // T Flip-Flop
    {
        XspiceModelDef d;
        d.name = "T Flip-Flop"; d.category = "Digital Logic"; d.spiceType = "d_tff";
        d.description = "Toggle flip-flop";
        d.inputPinCount = 1; d.outputPinCount = 2;
        d.pins = {{"T", XspicePinDef::Digital}, {"CLK", XspicePinDef::Digital},
                  {"Q", XspicePinDef::Digital}, {"NQ", XspicePinDef::Digital}};
        addDigitalParam(d); db.append(d);
    }

    // SR Latch
    {
        XspiceModelDef d;
        d.name = "SR Latch"; d.category = "Digital Logic"; d.spiceType = "d_srlatch";
        d.description = "Set-reset latch";
        d.inputPinCount = 2; d.outputPinCount = 2;
        d.pins = {{"S", XspicePinDef::Digital}, {"R", XspicePinDef::Digital},
                  {"Q", XspicePinDef::Digital}, {"NQ", XspicePinDef::Digital}};
        addDigitalParam(d); db.append(d);
    }

    // PWM
    {
        XspiceModelDef d;
        d.name = "PWM Generator"; d.category = "Digital Logic"; d.spiceType = "d_pwm";
        d.description = "Pulse-width modulator: control voltage maps to duty cycle";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"CTRL", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Digital}};
        addParam(d, "frequency", 1e6, "PWM frequency (Hz)", XspiceParamDef::SpinboxDouble, 1, 1e12);
        addParam(d, "duty_min", 0.0, "Duty cycle at min control voltage", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "duty_max", 1.0, "Duty cycle at max control voltage", XspiceParamDef::SpinboxDouble, 0, 1);
        db.append(d);
    }

    // Digital Oscillator
    {
        XspiceModelDef d;
        d.name = "Digital Oscillator"; d.category = "Digital Logic"; d.spiceType = "d_osc";
        d.description = "Voltage-controlled digital oscillator";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"CTRL", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Digital}};
        addParam(d, "freq_center", 1e6, "Center frequency (Hz)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "freq_dev", 5e5, "Frequency deviation (Hz/V)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "duty_cycle", 0.5, "Duty cycle (0-1)", XspiceParamDef::SpinboxDouble, 0, 1);
        db.append(d);
    }

    // ── ADC / DAC Bridges ───────────────────────────────────────────────────

    {
        XspiceModelDef d;
        d.name = "ADC Bridge"; d.category = "Mixed Signal"; d.spiceType = "adc_bridge";
        d.description = "Converts analog voltage to digital logic levels";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"A", XspicePinDef::VoltageIn}, {"D", XspicePinDef::Digital}};
        addParam(d, "in_low", 0.1, "Input voltage for logic 0 threshold");
        addParam(d, "in_high", 0.9, "Input voltage for logic 1 threshold");
        addParam(d, "rise_delay", 1e-9, "Rise delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "fall_delay", 1e-9, "Fall delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        db.append(d);
    }
    {
        XspiceModelDef d;
        d.name = "DAC Bridge"; d.category = "Mixed Signal"; d.spiceType = "dac_bridge";
        d.description = "Converts digital logic levels to analog voltage";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"D", XspicePinDef::Digital}, {"A", XspicePinDef::Conductance}};
        addParam(d, "out_low", 0.0, "Output voltage for logic 0");
        addParam(d, "out_high", 1.0, "Output voltage for logic 1");
        db.append(d);
    }

    // ── XtraDev ──────────────────────────────────────────────────────────────

    {
        XspiceModelDef d;
        d.name = "Memristor"; d.category = "Extended Devices"; d.spiceType = "memristor";
        d.description = "Threshold-type memristor (Pershin/Di Ventra)";
        d.inputPinCount = 2; d.outputPinCount = 0;
        d.pins = {{"PLUS", XspicePinDef::Conductance}, {"MINUS", XspicePinDef::Conductance}};
        addParam(d, "rmin", 10.0, "Minimum resistance (ohms)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "rmax", 10000.0, "Maximum resistance (ohms)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "rinit", 7000.0, "Initial resistance (ohms)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "vt", 0.0, "Threshold voltage (V)");
        db.append(d);
    }

    {
        XspiceModelDef d;
        d.name = "Potentiometer"; d.category = "Extended Devices"; d.spiceType = "potentiometer";
        d.description = "3-terminal potentiometer with configurable wiper position";
        d.inputPinCount = 3; d.outputPinCount = 0;
        d.pins = {{"R0", XspicePinDef::Conductance}, {"WIPER", XspicePinDef::Conductance}, {"R1", XspicePinDef::Conductance}};
        addParam(d, "position", 0.5, "Wiper position (0-1)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "resistance", 100000.0, "Total resistance (ohms)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "log", 0.0, "Logarithmic taper (1=yes, 0=no)", XspiceParamDef::Checkbox);
        db.append(d);
    }

    // ── Additional Analog Blocks ──────────────────────────────────────────

    // file_source
    {
        XspiceModelDef d;
        d.name = "File Source"; d.category = "Analog Behavioral"; d.spiceType = "file_source";
        d.description = "Arbitrary source from file data (time-value pairs)";
        d.inputPinCount = 0; d.outputPinCount = 1;
        d.pins = {{"OUT", XspicePinDef::Conductance}};
        addParam(d, "file_name", "data.txt", "Data file path", XspiceParamDef::LineEdit);
        addParam(d, "interpolate", 1.0, "Enable interpolation (1=yes, 0=no)", XspiceParamDef::Checkbox);
        addParam(d, "repeat", 0.0, "Repeat after end (1=yes, 0=no)", XspiceParamDef::Checkbox);
        db.append(d);
    }

    // multi_input_pwl
    {
        XspiceModelDef d;
        d.name = "Multi-Input PWL"; d.category = "Analog Behavioral"; d.spiceType = "multi_input_pwl";
        d.description = "Piecewise-linear function of multiple input voltages";
        d.inputPinCount = 2; d.outputPinCount = 1;
        d.pins = {{"IN0", XspicePinDef::VoltageIn}, {"IN1", XspicePinDef::VoltageIn, 2, true}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "points", "0 0 1 1", "PWL points: x0 y0 x1 y1 ...", XspiceParamDef::LineEdit);
        addParam(d, "n_inputs", 2.0, "Number of inputs (2-16)", XspiceParamDef::SpinboxInt, 2, 16);
        db.append(d);
    }

    // pwlts
    {
        XspiceModelDef d;
        d.name = "PWL Time-Scaled"; d.category = "Analog Behavioral"; d.spiceType = "pwlts";
        d.description = "PWL source with independent time scaling";
        d.inputPinCount = 0; d.outputPinCount = 1;
        d.pins = {{"OUT", XspicePinDef::Conductance}};
        addParam(d, "x_values", "0 1e-6 2e-6", "Time breakpoints", XspiceParamDef::LineEdit);
        addParam(d, "y_values", "0 1 0", "Voltage breakpoints", XspiceParamDef::LineEdit);
        addParam(d, "repeat", 0.0, "Repeat after end (1=yes, 0=no)", XspiceParamDef::Checkbox);
        db.append(d);
    }

    // xfer (simpler transfer function than s_xfer)
    {
        auto d = addAnalog("Transfer Function", "xfer", "Simple gain/phase transfer block");
        addParam(d, "gain", 1.0, "Gain");
        addParam(d, "offset", 0.0, "Output offset");
        db.append(d);
    }

    // astate
    {
        XspiceModelDef d;
        d.name = "Analog State Machine"; d.category = "Analog Behavioral"; d.spiceType = "astate";
        d.description = "Analog state machine with configurable states and transitions";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"IN", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "state_count", 2.0, "Number of states", XspiceParamDef::SpinboxInt, 1, 64);
        addParam(d, "delay", 0.0, "State transition delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        db.append(d);
    }

    // climit / ilimit
    {
        auto d = addAnalog("Current Limiter", "climit", "Current-limiting block with soft clamp");
        addParam(d, "limit", 1e-3, "Current limit (A)");
        addParam(d, "gain", 1.0, "Linear gain within limit");
        db.append(d);
    }

    // ── Additional Digital Logic ───────────────────────────────────────────

    // d_fdiv
    {
        XspiceModelDef d;
        d.name = "Frequency Divider"; d.category = "Digital Logic"; d.spiceType = "d_fdiv";
        d.description = "Digital frequency divider";
        d.inputPinCount = 3; d.outputPinCount = 1;
        d.pins = {{"CLK", XspicePinDef::Digital}, {"RST", XspicePinDef::Digital}, {"EN", XspicePinDef::Digital},
                  {"Q", XspicePinDef::Digital}};
        addDigitalParam(d);
        addParam(d, "divide_by", 2.0, "Division ratio", XspiceParamDef::SpinboxInt, 1, 65536);
        db.append(d);
    }

    // d_lut
    {
        XspiceModelDef d;
        d.name = "Look-Up Table"; d.category = "Digital Logic"; d.spiceType = "d_lut";
        d.description = "General-purpose digital look-up table";
        d.inputPinCount = 2; d.outputPinCount = 1;
        d.pins = {{"A0", XspicePinDef::Digital}, {"A1", XspicePinDef::Digital, 2, true},
                  {"Y", XspicePinDef::Digital}};
        addDigitalParam(d);
        addParam(d, "lut_values", "0 1 1 0", "LUT output values (space-separated)", XspiceParamDef::LineEdit);
        addParam(d, "address_bits", 2.0, "Number of address bits", XspiceParamDef::SpinboxInt, 1, 16);
        db.append(d);
    }

    // d_genlut
    {
        XspiceModelDef d;
        d.name = "Generic LUT"; d.category = "Digital Logic"; d.spiceType = "d_genlut";
        d.description = "Generalized multi-output look-up table with file loading";
        d.inputPinCount = 2; d.outputPinCount = 1;
        d.pins = {{"IN", XspicePinDef::Digital, 2, true}, {"OUT", XspicePinDef::Digital}};
        addDigitalParam(d);
        addParam(d, "lut_file", "", "LUT definition file (optional)", XspiceParamDef::LineEdit);
        db.append(d);
    }

    // d_ram
    {
        XspiceModelDef d;
        d.name = "Digital RAM"; d.category = "Digital Logic"; d.spiceType = "d_ram";
        d.description = "Random-access memory with configurable width and depth";
        d.inputPinCount = 4; d.outputPinCount = 1;
        d.pins = {{"ADDR", XspicePinDef::Digital, 2, true},
                  {"DIN", XspicePinDef::Digital, 2, true},
                  {"WE", XspicePinDef::Digital}, {"OE", XspicePinDef::Digital},
                  {"DOUT", XspicePinDef::Digital, 2, true}};
        addDigitalParam(d);
        addParam(d, "addr_bits", 4.0, "Address line count", XspiceParamDef::SpinboxInt, 1, 32);
        addParam(d, "data_bits", 4.0, "Data line count", XspiceParamDef::SpinboxInt, 1, 64);
        addParam(d, "init_file", "", "Initialization file", XspiceParamDef::LineEdit);
        db.append(d);
    }

    // d_open_c / d_open_e
    {
        auto d = addDigital("Open-Collector Buffer", "d_open_c", "Open-collector output buffer", 1, 1, {"IN", "OUT"});
        addDigitalParam(d);
        db.append(d);
    }
    {
        auto d = addDigital("Open-Emitter Buffer", "d_open_e", "Open-emitter output buffer", 1, 1, {"IN", "OUT"});
        addDigitalParam(d);
        db.append(d);
    }

    // d_pulldown / d_pullup
    {
        XspiceModelDef d;
        d.name = "Pull-Down"; d.category = "Digital Logic"; d.spiceType = "d_pulldown";
        d.description = "Digital pull-down resistor";
        d.inputPinCount = 0; d.outputPinCount = 1;
        d.pins = {{"OUT", XspicePinDef::Digital}};
        db.append(d);
    }
    {
        XspiceModelDef d;
        d.name = "Pull-Up"; d.category = "Digital Logic"; d.spiceType = "d_pullup";
        d.description = "Digital pull-up resistor";
        d.inputPinCount = 0; d.outputPinCount = 1;
        d.pins = {{"OUT", XspicePinDef::Digital}};
        db.append(d);
    }

    // d_source
    {
        XspiceModelDef d;
        d.name = "Digital Stimulus"; d.category = "Digital Logic"; d.spiceType = "d_source";
        d.description = "Digital stimulus source with configurable waveform";
        d.inputPinCount = 0; d.outputPinCount = 1;
        d.pins = {{"OUT", XspicePinDef::Digital}};
        addDigitalParam(d);
        addParam(d, "init_state", 1.0, "Initial state (0/1)", XspiceParamDef::SpinboxInt, 0, 1);
        addParam(d, "pattern", "1010", "Output pattern (0/1 string)", XspiceParamDef::LineEdit);
        addParam(d, "period", 1e-6, "Bit period (s)", XspiceParamDef::SpinboxDouble, 1e-15, 1);
        db.append(d);
    }

    // D-Latch
    {
        XspiceModelDef d;
        d.name = "D Latch"; d.category = "Digital Logic"; d.spiceType = "d_dlatch";
        d.description = "D-type transparent latch";
        d.inputPinCount = 2; d.outputPinCount = 2;
        d.pins = {{"D", XspicePinDef::Digital}, {"EN", XspicePinDef::Digital},
                  {"Q", XspicePinDef::Digital}, {"NQ", XspicePinDef::Digital}};
        addDigitalParam(d);
        addParam(d, "clk_delay", 1e-9, "Clock delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        db.append(d);
    }

    // SR Flip-Flop
    {
        XspiceModelDef d;
        d.name = "SR Flip-Flop"; d.category = "Digital Logic"; d.spiceType = "d_srff";
        d.description = "Set-reset flip-flop with clock";
        d.inputPinCount = 3; d.outputPinCount = 2;
        d.pins = {{"S", XspicePinDef::Digital}, {"R", XspicePinDef::Digital}, {"CLK", XspicePinDef::Digital},
                  {"Q", XspicePinDef::Digital}, {"NQ", XspicePinDef::Digital}};
        addDigitalParam(d);
        db.append(d);
    }

    // ── Additional Mixed Signal ────────────────────────────────────────────

    // bidi_bridge
    {
        XspiceModelDef d;
        d.name = "Bidirectional Bridge"; d.category = "Mixed Signal"; d.spiceType = "bidi_bridge";
        d.description = "Bidirectional analog/digital signal bridge";
        d.inputPinCount = 2; d.outputPinCount = 2;
        d.pins = {{"A", XspicePinDef::VoltageIn}, {"D", XspicePinDef::Digital},
                  {"A_OUT", XspicePinDef::Conductance}, {"D_OUT", XspicePinDef::Digital}};
        addParam(d, "in_low", 0.1, "Input low threshold");
        addParam(d, "in_high", 0.9, "Input high threshold");
        addParam(d, "out_low", 0.0, "Output low voltage");
        addParam(d, "out_high", 1.0, "Output high voltage");
        db.append(d);
    }

    // ── Additional XtraDev ─────────────────────────────────────────────────

    // aswitch
    {
        XspiceModelDef d;
        d.name = "Analog Switch"; d.category = "Extended Devices"; d.spiceType = "aswitch";
        d.description = "Voltage-controlled analog switch (XSPICE)";
        d.inputPinCount = 3; d.outputPinCount = 0;
        d.pins = {{"CTRL", XspicePinDef::VoltageIn}, {"IN", XspicePinDef::Conductance}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "r_on", 10.0, "On resistance (ohms)", XspiceParamDef::SpinboxDouble, 0, 1e9);
        addParam(d, "r_off", 1e9, "Off resistance (ohms)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "v_on", 1.0, "Control voltage for on state (V)");
        addParam(d, "v_off", 0.0, "Control voltage for off state (V)");
        db.append(d);
    }

    // magnetic core
    {
        XspiceModelDef d;
        d.name = "Magnetic Core"; d.category = "Extended Devices"; d.spiceType = "core";
        d.description = "Non-linear magnetic core with hysteresis (J-A model)";
        d.inputPinCount = 2; d.outputPinCount = 0;
        d.pins = {{"PLUS", XspicePinDef::Conductance}, {"MINUS", XspicePinDef::Conductance}};
        addParam(d, "area", 1e-4, "Cross-sectional area (m^2)");
        addParam(d, "length", 1e-2, "Magnetic path length (m)");
        addParam(d, "n_turns", 100.0, "Number of turns", XspiceParamDef::SpinboxInt, 1, 1e6);
        addParam(d, "mu_r", 1000.0, "Relative permeability");
        db.append(d);
    }

    // lcouple
    {
        XspiceModelDef d;
        d.name = "Inductive Coupling"; d.category = "Extended Devices"; d.spiceType = "lcouple";
        d.description = "Mutual inductive coupling (XSPICE model)";
        d.inputPinCount = 4; d.outputPinCount = 0;
        d.pins = {{"PRI+", XspicePinDef::Conductance}, {"PRI-", XspicePinDef::Conductance},
                  {"SEC+", XspicePinDef::Conductance}, {"SEC-", XspicePinDef::Conductance}};
        addParam(d, "coupling", 0.9, "Coupling coefficient (0-1)", XspiceParamDef::SpinboxDouble, 0, 1);
        db.append(d);
    }

    // zener
    {
        XspiceModelDef d;
        d.name = "Zener Diode"; d.category = "Extended Devices"; d.spiceType = "zener";
        d.description = "Zener diode model (XSPICE)";
        d.inputPinCount = 2; d.outputPinCount = 0;
        d.pins = {{"ANODE", XspicePinDef::Conductance}, {"CATHODE", XspicePinDef::Conductance}};
        addParam(d, "bv", 5.6, "Breakdown voltage (V)");
        addParam(d, "is", 1e-14, "Saturation current (A)");
        addParam(d, "n", 1.0, "Ideality factor");
        db.append(d);
    }

    // sidiode
    {
        XspiceModelDef d;
        d.name = "Si Diode"; d.category = "Extended Devices"; d.spiceType = "sidiode";
        d.description = "Standard silicon diode model (XSPICE)";
        d.inputPinCount = 2; d.outputPinCount = 0;
        d.pins = {{"ANODE", XspicePinDef::Conductance}, {"CATHODE", XspicePinDef::Conductance}};
        addParam(d, "is", 1e-14, "Saturation current (A)");
        addParam(d, "n", 1.0, "Ideality factor");
        addParam(d, "rs", 0.0, "Series resistance (ohms)");
        db.append(d);
    }

    // pswitch
    {
        XspiceModelDef d;
        d.name = "Power Switch"; d.category = "Extended Devices"; d.spiceType = "pswitch";
        d.description = "Power semiconductor switch (XSPICE)";
        d.inputPinCount = 3; d.outputPinCount = 0;
        d.pins = {{"CTRL", XspicePinDef::VoltageIn}, {"DRAIN", XspicePinDef::Conductance}, {"SOURCE", XspicePinDef::Conductance}};
        addParam(d, "r_on", 0.1, "On resistance (ohms)", XspiceParamDef::SpinboxDouble, 0, 1e9);
        addParam(d, "r_off", 1e9, "Off resistance (ohms)", XspiceParamDef::SpinboxDouble, 0, 1e12);
        addParam(d, "v_th", 2.0, "Threshold voltage (V)");
        db.append(d);
    }

    // ── Event-Driven (XtraEvt) ─────────────────────────────────────────────

    // d_to_real
    {
        XspiceModelDef d;
        d.name = "Digital to Real"; d.category = "Event-Driven"; d.spiceType = "d_to_real";
        d.description = "Converts digital logic level to real signal";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"D", XspicePinDef::Digital}, {"R", XspicePinDef::Conductance}};
        addParam(d, "v0", 0.0, "Output level for logic 0");
        addParam(d, "v1", 1.0, "Output level for logic 1");
        db.append(d);
    }

    // real_gain
    {
        XspiceModelDef d;
        d.name = "Real Signal Gain"; d.category = "Event-Driven"; d.spiceType = "real_gain";
        d.description = "Gain block for real-typed event-driven signals";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"IN", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "gain", 1.0, "Gain factor");
        addParam(d, "offset", 0.0, "Offset");
        db.append(d);
    }

    // real_delay
    {
        XspiceModelDef d;
        d.name = "Real Signal Delay"; d.category = "Event-Driven"; d.spiceType = "real_delay";
        d.description = "Delay line for real-typed event-driven signals";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"IN", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "delay", 1e-6, "Delay time (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        db.append(d);
    }

    // real_to_v
    {
        XspiceModelDef d;
        d.name = "Real to Voltage"; d.category = "Event-Driven"; d.spiceType = "real_to_v";
        d.description = "Converts real-typed event-driven signal to voltage";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"R", XspicePinDef::VoltageIn}, {"V", XspicePinDef::Conductance}};
        addParam(d, "gain", 1.0, "Conversion gain (V/real)");
        db.append(d);
    }

    // ── Additional Analog (from VioMATRIXC) ──────────────────────────────

    // ilimit — current limiter with power rails
    {
        XspiceModelDef d;
        d.name = "Current Limiter with Rails"; d.category = "Analog Behavioral"; d.spiceType = "ilimit";
        d.description = "Current limiter with positive/negative supply rails";
        d.inputPinCount = 2; d.outputPinCount = 1;
        d.pins = {{"IN", XspicePinDef::VoltageIn}, {"POS", XspicePinDef::Conductance, 0, false},
                  {"NEG", XspicePinDef::Conductance, 0, false}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "in_offset", 0.0, "Input offset");
        addParam(d, "gain", 1.0, "Gain");
        addParam(d, "r_out_source", 1.0, "Sourcing resistance", XspiceParamDef::SpinboxDouble, 1e-9, 1e9);
        addParam(d, "r_out_sink", 1.0, "Sinking resistance", XspiceParamDef::SpinboxDouble, 1e-9, 1e9);
        addParam(d, "i_limit_source", 1e-3, "Source current limit (A)", XspiceParamDef::SpinboxDouble, 0, 1e6);
        addParam(d, "i_limit_sink", 1e-3, "Sink current limit (A)", XspiceParamDef::SpinboxDouble, 0, 1e6);
        addParam(d, "v_pwr_range", 1e-6, "Power supply voltage range");
        addParam(d, "i_source_range", 1e-9, "Source current range");
        addParam(d, "i_sink_range", 1e-9, "Sink current range");
        db.append(d);
    }

    // viospice_jit — native JIT compiled smart block
    {
        XspiceModelDef d;
        d.name = "JIT Smart Block"; d.category = "Analog Behavioral"; d.spiceType = "viospice_jit";
        d.description = "VioSpice native JIT-compiled smart signal block";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"IN", XspicePinDef::VoltageIn, 1, true}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "jit_id", "default_block", "JIT block identifier", XspiceParamDef::LineEdit);
        db.append(d);
    }

    // ── Additional Digital (from VioMATRIXC) ─────────────────────────────

    // d_state — state machine
    {
        XspiceModelDef d;
        d.name = "Digital State Machine"; d.category = "Digital Logic"; d.spiceType = "d_state";
        d.description = "Digital state machine with file-based transition table";
        d.inputPinCount = 2; d.outputPinCount = 1;
        d.pins = {{"IN", XspicePinDef::Digital, 1, true}, {"CLK", XspicePinDef::Digital},
                  {"RST", XspicePinDef::Digital, 0, false}, {"OUT", XspicePinDef::Digital, 1, true}};
        addDigitalParam(d);
        addParam(d, "reset_delay", 1e-9, "Reset delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "state_file", "state.txt", "State transition file", XspiceParamDef::LineEdit);
        db.append(d);
    }

    // d_process — external process bridge
    {
        XspiceModelDef d;
        d.name = "Digital Process"; d.category = "Digital Logic"; d.spiceType = "d_process";
        d.description = "Bridge to an external executable process";
        d.inputPinCount = 2; d.outputPinCount = 1;
        d.pins = {{"IN", XspicePinDef::Digital, 1, true}, {"CLK", XspicePinDef::Digital},
                  {"RST", XspicePinDef::Digital, 0, false}, {"OUT", XspicePinDef::Digital, 1, true}};
        addDigitalParam(d);
        addParam(d, "reset_delay", 1e-9, "Reset delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "process_file", "", "Executable process path", XspiceParamDef::LineEdit);
        db.append(d);
    }

    // d_cosim — co-simulation bridge
    {
        XspiceModelDef d;
        d.name = "Co-Simulation Bridge"; d.category = "Digital Logic"; d.spiceType = "d_cosim";
        d.description = "Bridge to an irreversible digital model in a shared library";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"D_IN", XspicePinDef::Digital, 1, true},
                  {"D_OUT", XspicePinDef::Digital, 1, true},
                  {"D_IO", XspicePinDef::Digital, 1, true}};
        addParam(d, "delay", 1e-9, "Output delay (s)", XspiceParamDef::SpinboxDouble, 1e-12, 1);
        addParam(d, "simulation", "", "Shared library path", XspiceParamDef::LineEdit);
        addParam(d, "lib_args", "", "Library argument strings", XspiceParamDef::LineEdit);
        db.append(d);
    }

    // ── Additional XtraDev (from VioMATRIXC) ─────────────────────────────

    // capacitoric — capacitor with IC
    {
        XspiceModelDef d;
        d.name = "Capacitor with IC"; d.category = "Extended Devices"; d.spiceType = "capacitoric";
        d.description = "Capacitor with voltage initial condition (XSPICE)";
        d.inputPinCount = 2; d.outputPinCount = 0;
        d.pins = {{"PLUS", XspicePinDef::Conductance}, {"MINUS", XspicePinDef::Conductance}};
        addParam(d, "c", 1e-12, "Capacitance (F)", XspiceParamDef::SpinboxDouble, 1e-18, 1);
        addParam(d, "ic", 0.0, "Voltage initial condition (V)");
        db.append(d);
    }

    // inductoric — inductor with IC
    {
        XspiceModelDef d;
        d.name = "Inductor with IC"; d.category = "Extended Devices"; d.spiceType = "inductoric";
        d.description = "Inductor with current initial condition (XSPICE)";
        d.inputPinCount = 2; d.outputPinCount = 0;
        d.pins = {{"PLUS", XspicePinDef::Conductance}, {"MINUS", XspicePinDef::Conductance}};
        addParam(d, "l", 1e-6, "Inductance (H)", XspiceParamDef::SpinboxDouble, 1e-18, 1);
        addParam(d, "ic", 0.0, "Current initial condition (A)");
        db.append(d);
    }

    // cmeter — capacitance meter
    {
        auto d = addAnalog("Capacitance Meter", "cmeter", "ATESSE 1 compatible capacitance meter");
        d.pins = {{"IN", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "gain", 1.0, "C-to-voltage conversion factor");
        db.append(d);
    }

    // lmeter — inductance meter
    {
        auto d = addAnalog("Inductance Meter", "lmeter", "ATESSE 1 compatible inductance meter");
        d.pins = {{"IN", XspicePinDef::VoltageIn}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "gain", 1.0, "L-to-voltage conversion factor");
        db.append(d);
    }

    // seegen — single event effect generator
    {
        XspiceModelDef d;
        d.name = "SEE Generator"; d.category = "Extended Devices"; d.spiceType = "seegen";
        d.description = "Single-event effect (radiation) generator for fault injection";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"CTRL", XspicePinDef::VoltageIn}, {"MON", XspicePinDef::VoltageIn, 0, false},
                  {"OUT", XspicePinDef::Conductance}};
        addParam(d, "tfall", 500e-12, "Pulse fall time (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "trise", 20e-12, "Pulse rise time (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "tdelay", 0.0, "Pulse delay (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "inull", 0.0, "Max current (A)");
        addParam(d, "tperiod", 0.0, "Pulse repetition period (s)", XspiceParamDef::SpinboxDouble, 0, 1);
        addParam(d, "ctrlthres", 0.5, "Control voltage threshold (V)");
        addParam(d, "let", 10.0, "Linear energy transfer (MeV*cm^2/mg)");
        addParam(d, "cdepth", 1.0, "Charge collection depth (um)");
        addParam(d, "angle", 0.0, "Particle angle (rad)", XspiceParamDef::SpinboxDouble, 0, 1.57079);
        addParam(d, "perlim", 1.0, "Pulse repetition limit (1=enable)", XspiceParamDef::Checkbox);
        db.append(d);
    }

    // ── Transmission Lines ────────────────────────────────────────────────

    // tline
    {
        XspiceModelDef d;
        d.name = "Transmission Line"; d.category = "Transmission Lines"; d.spiceType = "tline";
        d.description = "Generic lossy transmission line";
        d.inputPinCount = 4; d.outputPinCount = 0;
        d.pins = {{"IN+", XspicePinDef::Conductance}, {"IN-", XspicePinDef::Conductance},
                  {"OUT+", XspicePinDef::Conductance}, {"OUT-", XspicePinDef::Conductance}};
        addParam(d, "l", 1.0, "Length (m)");
        addParam(d, "z", 50.0, "Characteristic impedance (ohms)");
        addParam(d, "a", 0.0, "Attenuation (dB/m)");
        addParam(d, "f", 1e9, "Frequency for attenuation (Hz)", XspiceParamDef::SpinboxDouble, 0, 1e15);
        db.append(d);
    }

    // cpline
    {
        XspiceModelDef d;
        d.name = "Coupled Line"; d.category = "Transmission Lines"; d.spiceType = "cpline";
        d.description = "Coupled transmission line pair";
        d.inputPinCount = 4; d.outputPinCount = 0;
        d.pins = {{"P1", XspicePinDef::Conductance}, {"P2", XspicePinDef::Conductance},
                  {"P3", XspicePinDef::Conductance}, {"P4", XspicePinDef::Conductance}};
        addParam(d, "l", 1.0, "Length (m)");
        addParam(d, "ze", 50.0, "Even-mode impedance (ohms)");
        addParam(d, "zo", 50.0, "Odd-mode impedance (ohms)");
        addParam(d, "ae", 0.0, "Even-mode attenuation (dB/m)");
        addParam(d, "ao", 0.0, "Odd-mode attenuation (dB/m)");
        addParam(d, "ere", 1.0, "Even-mode relative permittivity");
        addParam(d, "ero", 1.0, "Odd-mode relative permittivity");
        db.append(d);
    }

    // cpmlin
    {
        XspiceModelDef d;
        d.name = "Coupled Microstrip"; d.category = "Transmission Lines"; d.spiceType = "cpmlin";
        d.description = "Coupled microstrip transmission line";
        d.inputPinCount = 4; d.outputPinCount = 0;
        d.pins = {{"P1", XspicePinDef::Conductance}, {"P2", XspicePinDef::Conductance},
                  {"P3", XspicePinDef::Conductance}, {"P4", XspicePinDef::Conductance}};
        addParam(d, "l", 1e-2, "Length (m)");
        addParam(d, "w", 1e-3, "Width (m)");
        addParam(d, "s", 1e-3, "Spacing (m)");
        addParam(d, "er", 9.8, "Substrate relative permittivity");
        addParam(d, "h", 1e-3, "Substrate height (m)");
        addParam(d, "t", 35e-6, "Metal thickness (m)");
        addParam(d, "tand", 2e-4, "Loss tangent");
        addParam(d, "rho", 0.022e-6, "Resistivity (ohms*m)");
        addParam(d, "d", 0.15e-6, "Surface roughness (m)");
        db.append(d);
    }

    // mlin
    {
        XspiceModelDef d;
        d.name = "Microstrip Line"; d.category = "Transmission Lines"; d.spiceType = "mlin";
        d.description = "Single microstrip transmission line";
        d.inputPinCount = 2; d.outputPinCount = 0;
        d.pins = {{"PORT1", XspicePinDef::Conductance}, {"PORT2", XspicePinDef::Conductance}};
        addParam(d, "l", 1e-2, "Length (m)");
        addParam(d, "w", 1e-3, "Width (m)");
        addParam(d, "er", 9.8, "Substrate relative permittivity");
        addParam(d, "h", 1e-3, "Substrate height (m)");
        addParam(d, "t", 35e-6, "Metal thickness (m)");
        addParam(d, "tand", 2e-4, "Loss tangent");
        addParam(d, "rho", 0.022e-6, "Resistivity (ohms*m)");
        addParam(d, "d", 0.15e-6, "Surface roughness (m)");
        db.append(d);
    }

    // msopen
    {
        XspiceModelDef d;
        d.name = "Microstrip Open"; d.category = "Transmission Lines"; d.spiceType = "msopen";
        d.description = "Microstrip open-end discontinuity";
        d.inputPinCount = 1; d.outputPinCount = 0;
        d.pins = {{"P1", XspicePinDef::Conductance}};
        addParam(d, "w", 1e-3, "Width (m)");
        addParam(d, "er", 9.8, "Substrate relative permittivity");
        addParam(d, "h", 1e-3, "Substrate height (m)");
        addParam(d, "t", 35e-6, "Metal thickness (m)");
        addParam(d, "tand", 2e-4, "Loss tangent");
        db.append(d);
    }

    // ── SPICE 2 Poly ─────────────────────────────────────────────────────

    // spice2poly
    {
        XspiceModelDef d;
        d.name = "SPICE2 Poly Source"; d.category = "SPICE 2 Poly"; d.spiceType = "spice2poly";
        d.description = "SPICE 2G6 compatible polynomial controlled source";
        d.inputPinCount = 1; d.outputPinCount = 1;
        d.pins = {{"IN", XspicePinDef::VoltageIn, 1, true}, {"OUT", XspicePinDef::Conductance}};
        addParam(d, "coef", "1.0 0.0", "Coefficient list (space-separated)", XspiceParamDef::LineEdit);
        addParam(d, "m", 1.0, "Multiplicator");
        db.append(d);
    }

    return db;
}

const QVector<XspiceModelDef>& XspiceBlockItem::modelDatabase() {
    static const QVector<XspiceModelDef> db = buildModelDB();
    return db;
}

// ─── XspiceBlockItem ─────────────────────────────────────────────────────────

XspiceBlockItem::XspiceBlockItem(QGraphicsItem* parent)
    : SchematicItem(parent) {
    setModelType("gain");
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
}

XspiceBlockItem::XspiceBlockItem(const QString& modelType, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setModelType(modelType);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
}

void XspiceBlockItem::setModelType(const QString& type) {
    m_modelType = type;
    rebuildPins();
    const XspiceModelDef* def = modelDef();
    if (def) {
        for (const auto& p : def->params) {
            if (!m_xspiceParams.contains(p.name))
                m_xspiceParams[p.name] = QJsonValue::fromVariant(p.defaultValue);
        }
    }
    // Populate paramExpressions so netlist generator and ECOPackage capture them
    clearParamExpressions();
    setParamExpression("xspice_modelType", m_modelType);
    setParamExpression("xspice_spiceType", def ? def->spiceType : m_modelType);
    setParamExpression("xspice_params", QJsonDocument(m_xspiceParams).toJson(QJsonDocument::Compact));
    update();
}

void XspiceBlockItem::rebuildPins() {
    const XspiceModelDef* def = modelDef();
    if (!def) {
        m_inputPinCount = 1;
        m_outputPinCount = 1;
        return;
    }
    int in = 0, out = 0;
    for (const auto& pin : def->pins) {
        if (pin.type == XspicePinDef::VoltageIn || pin.type == XspicePinDef::VoltageDiff
            || pin.type == XspicePinDef::Digital || pin.type == XspicePinDef::CurrentSense)
            in += pin.minCount;
        else
            out += pin.minCount;
    }
    m_inputPinCount = qMax(1, in);
    m_outputPinCount = qMax(0, out);
}

const XspiceModelDef* XspiceBlockItem::modelDef() const {
    for (const auto& d : modelDatabase()) {
        if (d.name == m_modelType) return &d;
    }
    return nullptr;
}

// ─── Bounding Rect & Paint ───────────────────────────────────────────────────

static constexpr qreal WIDTH = 80;
static constexpr qreal PIN_SPACING = 20;
static constexpr qreal MARGIN = 10;

QRectF XspiceBlockItem::boundingRect() const {
    qreal h = qMax(qreal(40), (qreal)qMax(m_inputPinCount, m_outputPinCount) * PIN_SPACING + MARGIN * 2);
    constexpr qreal PIN_TAIL = 10;
    return QRectF(-WIDTH / 2 - PIN_TAIL, -h / 2, WIDTH + PIN_TAIL * 2, h);
}

void XspiceBlockItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    QRectF r = boundingRect();
    qreal h = r.height();

    // Body rect (core box, excluding pin tails)
    QRectF body(-WIDTH / 2, -h / 2, WIDTH, h);

    // Background
    QColor bg(30, 30, 40);
    if (option->state & QStyle::State_Selected) {
        bg = QColor(40, 50, 80);
    }
    painter->setBrush(bg);
    painter->setPen(QPen(QColor(100, 140, 255), 1.5));
    painter->drawRoundedRect(body.adjusted(1, 1, -1, -1), 4, 4);

    // Model name
    QString displayName = m_modelType;
    const XspiceModelDef* def = modelDef();
    if (def) displayName = def->name;

    painter->setPen(Qt::white);
    QFont f = painter->font();
    f.setPointSize(8);
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(body, Qt::AlignCenter, displayName);

    // Pin name labels
    if (def) {
        f.setPointSize(6);
        f.setBold(false);
        painter->setFont(f);
        QPen pinPen(QColor(180, 200, 255));
        painter->setPen(pinPen);

        static constexpr qreal PIN_TAIL = 10;

        // Input pins on left
        int inIdx = 0;
        for (int i = 0; i < def->pins.size() && inIdx < m_inputPinCount; ++i) {
            if (def->pins[i].type == XspicePinDef::Digital || def->pins[i].type == XspicePinDef::VoltageIn
                || def->pins[i].type == XspicePinDef::VoltageDiff || def->pins[i].type == XspicePinDef::CurrentSense) {
                qreal y = -h / 2 + MARGIN + inIdx * PIN_SPACING + PIN_SPACING / 2;
                painter->drawLine(QPointF(-WIDTH / 2 - PIN_TAIL, y), QPointF(-WIDTH / 2, y));
                painter->drawText(QRectF(-WIDTH / 2 - PIN_TAIL + 2, y - 8, WIDTH / 2 - 4, 16),
                                  Qt::AlignLeft | Qt::AlignVCenter, def->pins[i].name);
                ++inIdx;
            }
        }

        // Output pins on right
        int outIdx = 0;
        for (int i = 0; i < def->pins.size(); ++i) {
            if (def->pins[i].type == XspicePinDef::Conductance) {
                qreal y = -h / 2 + MARGIN + outIdx * PIN_SPACING + PIN_SPACING / 2;
                painter->drawLine(QPointF(WIDTH / 2, y), QPointF(WIDTH / 2 + PIN_TAIL, y));
                painter->drawText(QRectF(2, y - 8, WIDTH / 2 - 4, 16),
                                  Qt::AlignRight | Qt::AlignVCenter, def->pins[i].name);
                ++outIdx;
            }
        }
    }
}

// ─── Pins / Connectivity ─────────────────────────────────────────────────────

QList<QPointF> XspiceBlockItem::connectionPoints() const {
    QList<QPointF> pts;
    qreal h = boundingRect().height();
    static constexpr qreal PIN_TAIL = 10;

    // Input pins on left — at end of tail
    for (int i = 0; i < m_inputPinCount; ++i) {
        qreal y = -h / 2 + MARGIN + i * PIN_SPACING + PIN_SPACING / 2;
        pts.append(QPointF(-WIDTH / 2 - PIN_TAIL, y));
    }
    // Output pins on right — at end of tail
    for (int i = 0; i < m_outputPinCount; ++i) {
        qreal y = -h / 2 + MARGIN + i * PIN_SPACING + PIN_SPACING / 2;
        pts.append(QPointF(WIDTH / 2 + PIN_TAIL, y));
    }
    return pts;
}

QString XspiceBlockItem::pinName(int index) const {
    const XspiceModelDef* def = modelDef();
    if (!def) return QString::number(index + 1);
    int i = 0;
    auto pts = connectionPoints();
    if (index >= pts.size()) return QString::number(index + 1);

    // Input pins
    int inIdx = 0;
    for (int p = 0; p < def->pins.size(); ++p) {
        if (def->pins[p].type == XspicePinDef::Digital || def->pins[p].type == XspicePinDef::VoltageIn
            || def->pins[p].type == XspicePinDef::VoltageDiff || def->pins[p].type == XspicePinDef::CurrentSense) {
            if (i++ == index) return def->pins[p].name;
        }
    }
    // Output pins
    for (int p = 0; p < def->pins.size(); ++p) {
        if (def->pins[p].type == XspicePinDef::Conductance) {
            if (i++ == index) return def->pins[p].name;
        }
    }
    return QString::number(index + 1);
}

QList<SchematicItem::PinElectricalType> XspiceBlockItem::pinElectricalTypes() const {
    auto pts = connectionPoints();
    QList<PinElectricalType> types;
    const XspiceModelDef* def = modelDef();

    int inIdx = 0;
    for (int p = 0; p < (def ? def->pins.size() : 0) && inIdx < m_inputPinCount; ++p) {
        if (def->pins[p].type == XspicePinDef::Digital)
            types << InputPin;
        else
            types << InputPin;
        ++inIdx;
    }
    while (types.size() < m_inputPinCount)
        types << InputPin;

    for (int i = 0; i < m_outputPinCount; ++i)
        types << OutputPin;

    // Fill remaining
    while (types.size() < pts.size())
        types << PassivePin;

    return types;
}

// ─── Serialization ───────────────────────────────────────────────────────────

QJsonObject XspiceBlockItem::toJson() const {
    QJsonObject json = SchematicItem::toJson();
    json["modelType"] = m_modelType;
    json["xspiceParams"] = m_xspiceParams;
    json["inputPinCount"] = m_inputPinCount;
    json["outputPinCount"] = m_outputPinCount;
    return json;
}

bool XspiceBlockItem::fromJson(const QJsonObject& json) {
    if (!SchematicItem::fromJson(json)) return false;
    if (json.contains("xspiceParams")) {
        m_xspiceParams = json["xspiceParams"].toObject();
    }
    if (json.contains("inputPinCount"))
        m_inputPinCount = json["inputPinCount"].toInt();
    if (json.contains("outputPinCount"))
        m_outputPinCount = json["outputPinCount"].toInt();
    if (json.contains("modelType")) {
        setModelType(json["modelType"].toString());
    }
    update();
    return true;
}

QMap<QString, QString> XspiceBlockItem::paramExpressionsForNetlist() const {
    QMap<QString, QString> pe;
    pe["xspice_modelType"] = m_modelType;
    pe["xspice_params"] = QJsonDocument(m_xspiceParams).toJson(QJsonDocument::Compact);
    return pe;
}

SchematicItem* XspiceBlockItem::clone() const {
    auto* item = new XspiceBlockItem(m_modelType);
    item->setPos(pos());
    item->setRotation(rotation());
    item->m_xspiceParams = m_xspiceParams;
    item->m_inputPinCount = m_inputPinCount;
    item->m_outputPinCount = m_outputPinCount;
    return item;
}
