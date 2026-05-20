# XSPICE Code Models & SystemVerilog Blocks

Custom analog/digital behaviour can be added to schematics using either
pre-built **XSPICE code models** or user-defined **SystemVerilog blocks**.

## XSPICE Code Models

XSPICE is ngspice's extension mechanism for adding custom analog and mixed-signal
primitives. The library includes 70+ code models covering:

*   **Analog**: integrator, differentiator, limiter, s-domain transfer functions,
    gain/offset, summing amplifier, multiplier, divider.
*   **Mixed-Signal**: ADC, DAC, Schmitt trigger, sample-and-hold, controlled
    sources (current/voltage).
*   **Digital**: logic gates (AND, OR, NAND, NOR, XOR, XNOR), D flip-flop,
    SR latch, counter.

### Placement & Property Dialog

1.  Right-click the block and choose *Properties*.
2.  In the property dialog browse the library for the model you need.
3.  Connect input and output pins according to the model's pin-out.
4.  The block auto-generates the correct `.model` and `A_` device line in the
    netlist.

### Supported Syntax (Netlist View)

```
A_U1 [in1 in2 ...] out U_model_name
.model U_model_name viospice_xtype(...)
```

The block automates this — you only need to wire pins on the schematic.

## SystemVerilog Blocks

SystemVerilog blocks let you write purely combinational logic in standard
Verilog and simulate it directly without any JIT or FluxScript dependency.

### Requirements

*   one or more `input logic` ports and at least one `output logic` port.
*   Only `assign` statements (continuous assignments) are supported.
*   The file is parsed with [slang](https://github.com/MikePopoloski/slang).

### Supported Operators

| Category  | Operators                          |
|-----------|------------------------------------|
| Bitwise   | `&`, `|`, `^`, `~`                 |
| Arithmetic| `+`, `-`, `*`, `/`                 |
| Comparison| `==`, `!=`, `>`, `<`               |

Ternary `? :` is not yet supported.

### Example: AND Gate (`and2.sv`)

```systemverilog
module and2(input logic a, input logic b, output logic y);
    assign y = a & b;
endmodule
```

### Example: Full Adder (`full_adder.sv`)

```systemverilog
module fa(input logic a, input logic b, input logic cin,
          output logic sum, output logic cout);
    assign sum = a ^ b ^ cin;
    assign cout = (a & b) | (a & cin) | (b & cin);
endmodule
```

### Usage

1.  Place a **SystemVerilog Block** from the component palette.
2.  Right-click → *Properties*, browse to your `.sv` file and select the module.
3.  The dialog parses the file and shows detected input/output ports.
4.  Wire the block's pins on the schematic.
5.  Run **Transient (.tran)** — the block uses a pure C++ interpreter
    (no LLVM or FluxScript).

### Simulation Engine

Each output pin is compiled into an expression tree (`EvalNode`) driven by the
`viospice_jit` XSPICE code model. The evaluation uses pre-generated C++
trampoline functions (up to 512 per design), making it fully portable and free
of runtime code generation.

### Limitations

*   Only combinational `assign` — no `always`, `initial`, or clocked blocks.
*   Signal widths are limited to 1 bit (`logic`).
*   Maximum 512 output pins across all SV blocks in a design.
