# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

import subprocess
import json
import os
import pytest
from pathlib import Path

# Path to the viora binary
VIORA_PATH = os.environ.get("VIORA_PATH", "./build/viora")
FIXTURES_DIR = Path(__file__).parent / "fixtures"

def run_viora(args):
    """Helper to run viora and return parsed JSON output."""
    cmd = [VIORA_PATH] + args
    env = os.environ.copy()
    env["VIORA_NO_DAEMON"] = "1"
    result = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        # Some commands might return non-zero but still output valid JSON (warnings)
        # but generally we expect 0.
        pass
    
    try:
        # Try to find JSON in the output (it might have some log messages before/after)
        output = result.stdout
        start_idx = output.find('{')
        if start_idx != -1:
            return json.loads(output[start_idx:])
        return output
    except json.JSONDecodeError:
        return output

def test_viora_help():
    result = subprocess.run([VIORA_PATH, "--help"], capture_output=True, text=True)
    assert result.returncode == 0
    assert "Usage: viora" in result.stdout

def test_schematic_query():
    sch_path = str(FIXTURES_DIR / "untitled.sch")
    data = run_viora(["schematic-query", sch_path, "--json"])
    assert isinstance(data, dict)
    assert "file" in data
    assert data["file"].endswith("untitled.sch")
    assert "components" in data
    assert isinstance(data["components"], list)

def test_schematic_bom():
    sch_path = str(FIXTURES_DIR / "untitled.sch")
    data = run_viora(["schematic-bom", sch_path, "--json"])
    assert isinstance(data, dict)
    assert "components" in data
    assert "groups" in data

def test_schematic_validate():
    sch_path = str(FIXTURES_DIR / "untitled.sch")
    data = run_viora(["schematic-validate", sch_path, "--json"])
    assert isinstance(data, dict)
    assert "summary" in data

def test_symbol_validate():
    # Create a temporary symbol file
    sym_content = {
        "name": "TestSymbol",
        "referencePrefix": "X",
        "primitives": [
            {
                "type": "pin",
                "x": 0,
                "y": 0,
                "number": 1,
                "name": "1",
                "orientation": "Right",
                "length": 10
            }
        ]
    }
    sym_path = "temp_symbol.viosym"
    with open(sym_path, "w") as f:
        json.dump(sym_content, f)
    
    try:
        data = run_viora(["symbol-validate", sym_path, "--json"])
        assert isinstance(data, dict)
        assert data["name"] == "TestSymbol"
        assert "issues" in data
    finally:
        if os.path.exists(sym_path):
            os.remove(sym_path)

def test_netlist_run_smart_signal():
    smart_sch_path = str(FIXTURES_DIR / "smart_signal_pwm.flxsch")
    data = run_viora([
        "netlist-run", smart_sch_path,
        "--analysis", "tran",
        "--stop", "2m",
        "--step", "5u",
        "--export-raw", "json",
        "--json"
    ])
    
    assert isinstance(data, dict)
    assert data.get("ok") is True
    assert "raw" in data
    raw = data["raw"]
    assert "signals" in raw
    
    # Check if OUT signal exists and toggles
    signals = raw["signals"]
    out_values = None
    for s in signals:
        if s["name"].upper() in ["V(OUT)", "OUT"]:
            out_values = s["values"]
            break
    
    assert out_values is not None
    assert len(out_values) > 0
    
    v_min = min(out_values)
    v_max = max(out_values)
    assert v_min < 0.1
    assert v_max > 4.0

def test_pcb_lifecycle_e2e():
    pcb_path = "temp_test_board.pcb"
    png_path = "temp_test_board.png"
    
    # Clean up any leftover files
    for p in [pcb_path, png_path]:
        if os.path.exists(p):
            os.remove(p)

    try:
        # 1. Initialize a standalone PCB
        init_data = run_viora(["pcb-init", pcb_path, "--width", "120", "--height", "90", "--layers", "4", "--json"])
        assert isinstance(init_data, dict)
        assert init_data.get("ok") is True
        assert init_data.get("width") == 120
        assert init_data.get("height") == 90
        assert init_data.get("layersCount") == 4
        assert os.path.exists(pcb_path)

        # 2. Query the PCB details
        query_data = run_viora(["pcb-query", pcb_path, "--json"])
        assert isinstance(query_data, dict)
        assert query_data.get("width") == 120
        assert query_data.get("height") == 90
        assert query_data.get("layersCount") == 4
        assert "components" in query_data
        assert "nets" in query_data

        # 3. Run DRC validation on the PCB
        val_data = run_viora(["pcb-validate", pcb_path, "--json"])
        assert isinstance(val_data, dict)
        assert "drc" in val_data
        assert "summary" in val_data
        assert val_data["summary"].get("errorCount") == 0

        # 4. Render the PCB layout to PNG
        render_data = run_viora(["pcb-render", pcb_path, png_path, "--scale", "2", "--json"])
        assert isinstance(render_data, dict)
        assert render_data.get("width") > 0
        assert render_data.get("height") > 0
        assert os.path.exists(png_path)

    finally:
        # Cleanup
        for p in [pcb_path, png_path]:
            if os.path.exists(p):
                os.remove(p)

def test_pcb_init_from_schematic():
    pcb_path = "temp_sync_board.pcb"
    sch_path = str(FIXTURES_DIR / "untitled.sch")
    
    if os.path.exists(pcb_path):
        os.remove(pcb_path)

    try:
        # Initialize PCB from schematic
        init_data = run_viora(["pcb-init", pcb_path, "--schematic", sch_path, "--json"])
        assert isinstance(init_data, dict)
        assert init_data.get("ok") is True
        assert os.path.exists(pcb_path)

        # Verify components were imported
        query_data = run_viora(["pcb-query", pcb_path, "--json"])
        assert isinstance(query_data, dict)
        assert query_data.get("componentsCount") > 0

    finally:
        if os.path.exists(pcb_path):
            os.remove(pcb_path)

def test_pcb_compose():
    pcb_path = "temp_comp.pcb"
    if os.path.exists(pcb_path):
        os.remove(pcb_path)

    try:
        # 1. Compose items into a new board
        comp_data = run_viora([
            "pcb-compose", pcb_path,
            "--add-component", "footprint=R_0805,x=20,y=30,rotation=90,layer=0,name=R1,value=10k",
            "--add-trace", "x1=20,y1=30,x2=50,y2=30,width=0.3,layer=0,net=NET_R1",
            "--add-via", "x=50,y=30,diameter=0.9,drill=0.4,net=NET_R1",
            "--json"
        ])
        assert isinstance(comp_data, dict)
        assert comp_data.get("ok") is True
        assert comp_data.get("addedComponents") == 1
        assert comp_data.get("addedTraces") == 1
        assert comp_data.get("addedVias") == 1
        assert os.path.exists(pcb_path)

        # 2. Query composition
        query_data = run_viora(["pcb-query", pcb_path, "--json"])
        assert isinstance(query_data, dict)
        assert query_data.get("componentsCount") == 1
        assert query_data.get("viasCount") == 1
        assert "NET_R1" in query_data.get("nets", [])

        # 3. Delete component
        del_data = run_viora([
            "pcb-compose", pcb_path,
            "--delete-item", "name=R1",
            "--json"
        ])
        assert isinstance(del_data, dict)
        assert del_data.get("deletedItems") == 1

        # 4. Query again to verify deletion
        query_data_after = run_viora(["pcb-query", pcb_path, "--json"])
        assert isinstance(query_data_after, dict)
        assert query_data_after.get("componentsCount") == 0

    finally:
        if os.path.exists(pcb_path):
            os.remove(pcb_path)

def test_footprint_list():
    # 1. Test pagination limits
    list_data = run_viora(["footprint-list", "--limit", "10", "--json"])
    assert isinstance(list_data, dict)
    assert "footprints" in list_data
    assert list_data.get("limit") == 10
    assert list_data.get("offset") == 0
    assert len(list_data["footprints"]) <= 10
    
    # 2. Test querying by string filter
    query_data = run_viora(["footprint-list", "--query", "0805", "--json"])
    assert isinstance(query_data, dict)
    assert "footprints" in query_data
    for fp in query_data["footprints"]:
        assert "0805" in fp["name"].lower()

def test_pcb_compose_auto_route():
    pcb_path = "temp_route.pcb"
    if os.path.exists(pcb_path):
        os.remove(pcb_path)

    try:
        # Compose components and request auto-routing
        comp_data = run_viora([
            "pcb-compose", pcb_path,
            "--add-component", "footprint=R_0805,x=20,y=30,rotation=0,layer=0,name=R1,value=10k",
            "--add-component", "footprint=R_0805,x=50,y=30,rotation=0,layer=0,name=R2,value=10k",
            "--add-via", "x=20,y=30,diameter=0.8,drill=0.4,net=NET1",
            "--add-via", "x=50,y=30,diameter=0.8,drill=0.4,net=NET1",
            "--auto-route", "--json"
        ])
        assert isinstance(comp_data, dict)
        assert comp_data.get("ok") is True
        assert "routedConnections" in comp_data
        assert os.path.exists(pcb_path)

    finally:
        if os.path.exists(pcb_path):
            os.remove(pcb_path)

if __name__ == "__main__":
    pytest.main([__file__])
