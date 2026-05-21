import sys
from pathlib import Path
import json

# Add project root to sys.path to import vspice
ROOT = Path(__file__).resolve().parent
sys.path.append(str(ROOT / "python"))

from vspice import mcp

# 1. Load the Simple JIT FluxScript
flux_code = Path("examples/simple_jit.flux").read_text()

# 2. Run simulation with a simple load
print("Running Simple JIT Signal simulation...")
result = mcp.netlist_run(
    cir="RLOAD OUT 0 1k\n.tran 1u 1m\n.save all",
    smart_signals=[
        {
            "ref": "SIG_GEN",
            "code": flux_code,
            "inputs": [],
            "outputs": ["OUT"]
        }
    ]
)

if result.get("ok"):
    print(f"✓ Simulation successful!")
    print(json.dumps(result, indent=2))
    # Launch viewer
    if result.get('rawPath'):
        mcp.launch_viewer(result.get('rawPath'))
else:
    print(f"✗ Simulation failed: {result.get('error')}")
    if "stdout" in result: print(result["stdout"])
    if "stderr" in result: print(result["stderr"])
