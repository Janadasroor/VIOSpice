import sys
from pathlib import Path
import json
import subprocess

# Add project root to sys.path to import vspice
ROOT = Path(__file__).resolve().parent
sys.path.append(str(ROOT / "python"))

from vspice import mcp

# 1. Verilog Code (AND gate)
sv_code = """
module my_and(input logic a, input logic b, output logic y);
    assign y = a & b;
endmodule
"""

# 2. Setup Debug Schematic
items = []
sv_path = "test_logic.sv"
Path(sv_path).write_text(sv_code)

module_name = "my_and"
items.append({
    "type": "SystemVerilogBlock",
    "reference": "U1",
    "value": str(Path(sv_path).absolute()),
    "svFilePath": str(Path(sv_path).absolute()),
    "moduleName": module_name,
    "systemVerilogModule": module_name,
    "excludeFromSim": True
})

hybrid_netlist = """
V1 1 0 PULSE(0 5 0 1u 1u 1m 2m)
V2 2 0 PULSE(0 5 0 1u 1u 2m 4m)
RLOAD OUT 0 1k
.tran 10u 5m
.save all
"""
inspect_res = mcp.verilog_inspect(sv_path, module=module_name)
print(f"Inspect Result: {inspect_res}")
ports = inspect_res.get("data", {}).get("ports", [])
if not ports:
    print("FAILED to get ports from inspect_res")
    sys.exit(1)
in_ports = [p["name"] for p in ports if p["direction"] == "input"]
out_ports = [p["name"] for p in ports if p["direction"] == "output"]
in_nets = ["1", "2"]
in_vector = "[" + " ".join(in_nets) + "]"
for out_pin in out_ports:
    jit_id = f"U1_{out_pin.upper()}"
    hybrid_netlist += f"A_{jit_id} {in_vector} OUT viospice_jit_model_{jit_id}\n"
    hybrid_netlist += f".model viospice_jit_model_{jit_id} viospice_jit (jit_id=\"{jit_id}\")\n"

items.append({
    "type": "Spice Directive",
    "text": hybrid_netlist
})

sch_json = {
    "metadata": {"application": "viospice", "version": 1},
    "items": items
}
Path("test_debug.flxsch").write_text(json.dumps(sch_json, indent=2))

print("Executing viora netlist-run test_debug.flxsch...")
proc = subprocess.run(["./build/viora", "netlist-run", "test_debug.flxsch"], capture_output=True, text=True)
print("STDOUT:")
print(proc.stdout)
print("STDERR:")
print(proc.stderr)
