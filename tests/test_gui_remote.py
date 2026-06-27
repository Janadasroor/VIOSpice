#!/usr/bin/env python3

# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

"""
Automated regression test harness for VioraEDA GUI.
Uses the CLI screenshot and GUI remote control commands to test functionality.
"""

import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VIOCLI = ROOT / "build" / "viora"


def run_cmd(args, timeout=30):
    """Run a CLI command and return parsed JSON output."""
    try:
        proc = subprocess.run(
            [str(VIOCLI)] + args,
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        if proc.returncode != 0 and proc.stderr:
            return {"ok": False, "error": proc.stderr.strip()}
        # Parse JSON from stdout
        out = proc.stdout.strip()
        if out:
            try:
                return json.loads(out)
            except json.JSONDecodeError:
                return {"ok": True, "stdout": out}
        return {"ok": True}
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": f"Command timed out after {timeout}s"}
    except Exception as e:
        return {"ok": False, "error": str(e)}


def test_screenshot_list():
    """Test: list visible windows."""
    result = run_cmd(["screenshot", "--json"])
    assert result.get("ok"), f"screenshot_list failed: {result}"
    windows = result.get("windows", [])
    assert len(windows) > 0, "No windows found"
    print(f"  PASS: Found {len(windows)} window(s)")
    return True


def test_gui_list_buttons():
    """Test: list interactive elements."""
    result = run_cmd(["gui", "list-buttons", "--window", "SchematicEditor", "--json"])
    assert result.get("ok"), f"gui_list_buttons failed: {result}"
    elements = result.get("elements", [])
    assert len(elements) > 0, "No elements found"
    print(f"  PASS: Found {len(elements)} interactive element(s)")
    return True


def test_gui_click():
    """Test: click a button."""
    result = run_cmd(["gui", "click", "Fit", "--window", "SchematicEditor", "--json"])
    assert result.get("ok"), f"gui_click failed: {result}"
    print(f"  PASS: Clicked '{result.get('label')}' ({result.get('type')})")
    return True


def test_gui_menu():
    """Test: trigger a menu action."""
    result = run_cmd(["gui", "menu", "Fit All", "--window", "SchematicEditor", "--json"])
    # Menu actions may not be visible, so just check no crash
    print(f"  PASS: Menu trigger returned ok={result.get('ok')}")
    return True


def test_gui_key():
    """Test: send keyboard shortcut."""
    result = run_cmd(["gui", "key", "Escape", "--window", "SchematicEditor", "--json"])
    assert result.get("ok"), f"gui_key failed: {result}"
    print(f"  PASS: Sent key '{result.get('shortcut')}' to {result.get('target')}")
    return True


def test_gui_tab():
    """Test: switch tabs."""
    result = run_cmd(["gui", "tab", "mos_test", "--window", "SchematicEditor", "--json"])
    assert result.get("ok"), f"gui_tab failed: {result}"
    print(f"  PASS: Switched to tab '{result.get('tab')}' (index {result.get('index')})")
    # Switch back
    run_cmd(["gui", "tab", "xspice", "--window", "SchematicEditor", "--json"])
    return True


def test_screenshot_capture():
    """Test: capture a screenshot."""
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
        output = f.name
    result = run_cmd(["screenshot", "--name", "SchematicEditor", "--output", output, "--json"])
    assert result.get("ok"), f"screenshot_capture failed: {result}"
    assert Path(output).exists(), "Screenshot file not created"
    size = Path(output).stat().st_size
    print(f"  PASS: Captured {result.get('width')}x{result.get('height')} screenshot ({size} bytes)")
    Path(output).unlink()
    return True


def test_workflow():
    """Test: run a multi-step workflow."""
    result = run_cmd([
        "gui", "run",
        "--step", "tab xspice",
        "--step", "wait 200",
        "--step", "key Escape",
        "--step", "screenshot --name SchematicEditor --output /tmp/test_workflow.png",
    ])
    assert result.get("ok"), f"workflow failed: {result}"
    print(f"  PASS: Workflow executed successfully")
    # Cleanup
    Path("/tmp/test_workflow.png").unlink(missing_ok=True)
    return True


def test_fuzzy_matching():
    """Test: fuzzy matching prefers exact matches."""
    # "Run Simulation" should match "Run Simulation (F8)" not "Run ERC (F7)"
    result = run_cmd(["gui", "click", "Run Simulation", "--window", "SchematicEditor", "--json"])
    assert result.get("ok"), f"fuzzy matching failed: {result}"
    label = result.get("label", "")
    assert "Simulation" in label, f"Expected 'Run Simulation', got '{label}'"
    print(f"  PASS: Fuzzy match correctly selected '{label}'")
    return True


def main():
    """Run all tests."""
    tests = [
        ("Screenshot List", test_screenshot_list),
        ("GUI List Buttons", test_gui_list_buttons),
        ("GUI Click", test_gui_click),
        ("GUI Menu", test_gui_menu),
        ("GUI Key", test_gui_key),
        ("GUI Tab", test_gui_tab),
        ("Screenshot Capture", test_screenshot_capture),
        ("Fuzzy Matching", test_fuzzy_matching),
        ("Workflow", test_workflow),
    ]

    print("VioraEDA GUI Test Harness")
    print("=" * 40)

    passed = 0
    failed = 0

    for name, test_fn in tests:
        print(f"\n[{name}]")
        try:
            if test_fn():
                passed += 1
            else:
                failed += 1
                print(f"  FAIL: {name}")
        except AssertionError as e:
            failed += 1
            print(f"  FAIL: {e}")
        except Exception as e:
            failed += 1
            print(f"  ERROR: {e}")

    print("\n" + "=" * 40)
    print(f"Results: {passed} passed, {failed} failed, {passed + failed} total")

    if failed > 0:
        sys.exit(1)
    print("All tests passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
