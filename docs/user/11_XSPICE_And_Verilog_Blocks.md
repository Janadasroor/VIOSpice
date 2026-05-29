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

*   One or more `input logic` ports and at least one `output logic` port.
*   `assign` statements targeting output ports.
*   `always_comb` with blocking assignments (`=`) targeting output ports.
*   The file is parsed with [slang](https://github.com/MikePopoloski/slang).

### Supported Constructs

| Category           | Constructs                                |
|--------------------|-------------------------------------------|
| Port types         | `logic`, `wire` (input/output)            |
| Continuous assign  | `assign out = expr;`                      |
| Always comb        | `always_comb begin out = expr; end`       |
| Bitwise            | `&`, `|`, `^`, `~`                        |
| Arithmetic         | `+`, `-`                                  |
| Ternary            | `? :` (mux/selector)                      |
| Concatenation      | `{a, b}` (RHS and LHS)                    |
| Multi-bit ports    | `[3:0]` bus syntax                        |
| Literals           | `4'b1010`, `4'd5`, `1'b0`, `1'b1`        |

### What NOT to use (not supported)

| Unsupported          | Instead use                               |
|----------------------|-------------------------------------------|
| `always @(*)`        | `always_comb`                             |
| `case` / `casex`     | ternary `? :` or bitwise logic            |
| `if` / `else`        | ternary `? :`                             |
| `wire x; assign x=`  | inline the expression, or use always_comb |
| `*`, `/`, `%`        | not available (use multiple +/assigns)    |
| `==`, `!=`, `<`, `>` | not available                             |
| Bit-select LHS       | use separate output per bit               |
| Part-select `[3:2]`  | not available                             |
| `for` loops          | not available                             |
| Internal variables   | not available (all signals are ports)     |

### Multi-Width Ternary Rule

When using ternary `? :` with an LHS wider than the operands, explicitly extend
the operands with concatenation to avoid width-mismatch issues:

```systemverilog
// CORRECT: operands explicitly extended to match output width
assign {carry, result} = op ? ({1'b0, a} - {1'b0, b}) : ({1'b0, a} + {1'b0, b});

// WRONG: slang adds implicit width conversion, can produce zeros
// assign {carry, result} = op ? (a - b) : (a + b);
```

### Example: 4-bit Calculator (ADD/SUB)

```systemverilog
module calc(
    input logic [3:0] a,
    input logic [3:0] b,
    input logic op,
    output logic [3:0] result,
    output logic carry
);
    assign {carry, result} = op ? ({1'b0, a} - {1'b0, b}) : ({1'b0, a} + {1'b0, b});
endmodule
```

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

### Example: 4-bit Adder with Carry

```systemverilog
module adder4(
    input logic [3:0] a,
    input logic [3:0] b,
    output logic [3:0] sum,
    output logic carry
);
    assign {carry, sum} = a + b;
endmodule
```

### Example: Multi-bit MUX via always_comb

```systemverilog
module mux2(
    input logic sel,
    input logic [3:0] a,
    input logic [3:0] b,
    output logic [3:0] y
);
    always_comb begin
        y = sel ? a : b;
    end
endmodule
```

### Usage via MCP (Python)

```python
from vspice.mcp import netlist_run

r = netlist_run(
    cir="V1 a0 0 DC 5\nV2 a1 0 DC 0\nR1 y0 0 1k",
    verilog_blocks=[{
        "ref": "U1",
        "code": "module top(...); ... endmodule",
        "module": "top",
        "inputs": ["a0", "a1", ...],    # per-bit, LSB-first
        "outputs": ["y0", "y1", ...]    # per-bit, LSB-first
    }],
    analysis="tran", stop="10u", step="1u"
)
```

Pin ordering: multi-bit ports are expanded LSB-first (bit 0 at the first index).
For a port `[3:0] a`, the 4 input pins are `[a0, a1, a2, a3]`.

### Simulation Engine

Each output pin is compiled into an expression tree (`EvalNode`) driven by the
`viospice_jit` XSPICE code model. The evaluation uses pre-generated C++
trampoline functions (up to 512 per design), making it fully portable and free
of runtime code generation. The message *"code models like analog.cm have not
been loaded successfully"* is benign and can be ignored.

### Limitations

*   Only combinational `assign` and `always_comb` — no sequential logic.
*   No `case`, `if/else`, `for`, or `function`/`task`.
*   Internal wires / variables are not supported — all signals must be ports.
*   Maximum 512 output pins across all SV blocks in a design.
