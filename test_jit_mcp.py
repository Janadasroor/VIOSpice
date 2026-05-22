import sys
from pathlib import Path
import json

# Add project root to sys.path to import vspice
ROOT = Path(__file__).resolve().parent
sys.path.append(str(ROOT / "python"))

from vspice import mcp

# 1. Run simulation with FluxScript JIT block via MCP
print("Running simulation with FluxScript JIT block...")
result = mcp.netlist_run(
    cir="""
V1 1 0 PULSE(0 5 0 1u 1u 1m 2m)
RLOAD OUT 0 1k
.tran 10u 5m
.save all
""",
    verilog_blocks=[ # We can use the same mechanism but with 'flux' engine if supported
        # Actually mcp.py netlist_run currently only supports verilog_blocks
        # Let's check mcp.py
    ]
)
