# AI Agent Quickstart: Verilog Blocks in MCP

Use this template to write Verilog blocks that work with `viospice_netlist_run`.

## Template: 4-bit Calculator

```systemverilog
module calc(
    input  logic [3:0] a,
    input  logic [3:0] b,
    input  logic       op,
    output logic [3:0] result,
    output logic       carry
);
    assign {carry, result} = op ? ({1'b0, a} - {1'b0, b}) : ({1'b0, a} + {1'b0, b});
endmodule
```

## MCP Call

```python
from vspice.mcp import netlist_run

r = netlist_run(
    # SPICE netlist with DC sources for inputs, pull-downs for outputs
    cir="""V1 a0 0 DC 5
V2 a1 0 DC 0
V3 a2 0 DC 5
V4 a3 0 DC 0
V5 b0 0 DC 5
V6 b1 0 DC 5
V7 b2 0 DC 0
V8 b3 0 DC 0
V9 op 0 DC 0
R1 result0 0 1k
R2 result1 0 1k
R3 result2 0 1k
R4 result3 0 1k
R5 carry 0 1k""",
    verilog_blocks=[{
        "ref": "U1",
        "code": open("calc.sv").read(),
        "module": "calc",
        # LSB-first for each port: a[0..3], b[0..3], op
        "inputs":  ["a0","a1","a2","a3","b0","b1","b2","b3","op"],
        "outputs": ["result0","result1","result2","result3","carry"]
    }],
    analysis="tran",
    stop="10u",
    step="1u"
)
```

## Rules

| Do | Don't |
|----|-------|
| `assign out = a ^ b;` | `always @(*)` |
| `always_comb begin out = a; end` | `case (sel) ... endcase` |
| `{carry, sum} = a + b;` | `wire tmp; assign tmp = ...` |
| `op ? ext_a : ext_b` where ext_a/b are width-matched | `op ? a : b` when LHS is wider |
| Multi-bit: `input logic [3:0] a` | Internal `logic [3:0] tmp` |
| `always @(posedge clk) q <= d;` | Non-blocking `<=` outside sequential blocks |
| `always @(posedge clk or posedge rst) if (rst) q <= 0; else q <= d;` | |

## Key Facts

- **Benign warning**: "code models like analog.cm have not been loaded successfully" — ignore it
- **Available ops**: `&`, `|`, `^`, `~`, `+`, `-`, `?:`, `{}`
- **No width mismatch with ternary**: always extend operands to match output width
- **No internal wires**: every signal must be an input or output port
- **No bit-select on LHS**: create separate output bits instead
- **Parse raw results** via `r["raw_path"]` (binary `.raw` file)
- **DFF state** survives across timesteps; async reset supported (`posedge clk or posedge rst`)
