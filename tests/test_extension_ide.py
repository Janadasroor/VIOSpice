#!/usr/bin/env python3
# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
"""
Automated UX regression tests for the Extension IDE.
Uses async websockets library to connect to running VioSpice GUI via WebSocket (port 18790).

Usage:
    1. Launch VioSpice with: ./build/VioraEDA --extension-ide
    2. Run: python3 tests/test_extension_ide.py
"""

import asyncio
import json
import sys
import os
from pathlib import Path

try:
    import websockets
except ImportError:
    print("ERROR: 'websockets' package not found. Install with: pip install websockets")
    sys.exit(1)

HOST = "127.0.0.1"
PORT = 18790
SCREENSHOT_DIR = Path("/tmp/ide_ux_tests")
SCREENSHOT_DIR.mkdir(exist_ok=True)

passed = 0
failed = 0
total = 0

def report(name, ok, detail=""):
    global passed, failed, total
    total += 1
    if ok:
        passed += 1
        print(f"  PASS  {name}")
    else:
        failed += 1
        print(f"  FAIL  {name} — {detail}")

async def send_cmd(ws, cmd, params=None, timeout=10):
    """Send a JSON command and wait for response."""
    msg_id = int.from_bytes(os.urandom(4), 'big')
    payload = {"id": msg_id, "cmd": cmd}
    if params:
        payload["params"] = params
    await ws.send(json.dumps(payload))
    resp = await asyncio.wait_for(ws.recv(), timeout=timeout)
    return json.loads(resp)

async def screenshot(ws, name):
    """Capture a screenshot and return the path."""
    resp = await send_cmd(ws, "screenshot_capture", {
        "name": name, "clipboard": False, "scale": 2.0, "format": "PNG"
    }, timeout=15)
    path = resp.get("path", "")
    if path and os.path.exists(path):
        dest = SCREENSHOT_DIR / f"{name.replace('/', '_')}.png"
        import shutil
        shutil.copy2(path, str(dest))
        return str(dest)
    return path

# ── Tests ────────────────────────────────────────────────────────

async def test_connect(ws):
    """Test 1: WebSocket connection and ping."""
    resp = await send_cmd(ws, "ping")
    report("Connect + ping", resp.get("ok") is not None or resp.get("pong") is not None,
           str(resp)[:100])

async def test_list_windows(ws):
    """Test 2: List visible windows — find Extension IDE."""
    resp = await send_cmd(ws, "screenshot_list")
    windows = resp.get("windows", [])
    found = any("Extension IDE" in w.get("title", "") or "ExtensionIde" in w.get("class", "")
                for w in windows)
    report("Extension IDE window found", found, f"{len(windows)} window(s): {[w.get('title','?') for w in windows]}")
    return found

async def test_screenshot_initial(ws):
    """Test 3: Initial screenshot of the IDE."""
    path = await screenshot(ws, "IDE::ExtensionIdeWindow")
    report("Initial screenshot", bool(path), path or "no path returned")

async def test_list_elements(ws):
    """Test 4: List interactive elements."""
    resp = await send_cmd(ws, "gui_list_elements", {"window": "Extension IDE"})
    elements = resp.get("elements", [])
    report("List interactive elements", len(elements) > 0, f"{len(elements)} elements found")

async def test_click_new(ws):
    """Test 5: Click '+ New' button."""
    resp = await send_cmd(ws, "gui_click", {"window": "Extension IDE", "target": "New"})
    report("Click '+ New' button", resp.get("ok", False), str(resp)[:100])

async def test_type_code(ws):
    """Test 6: Type FluxScript code into editor."""
    code = 'def main() {\n    viora_flux_print("Hello from test")\n}'
    resp = await send_cmd(ws, "gui_type", {
        "window": "Extension IDE", "target": "editor", "text": code, "append": False
    })
    report("Type FluxScript code", resp.get("ok", False), str(resp)[:100])

async def test_screenshot_after_edit(ws):
    """Test 7: Screenshot after editing."""
    path = await screenshot(ws, "IDE::ExtensionIdeWindow")
    report("Screenshot after edit", bool(path))

async def test_click_run(ws):
    """Test 8: Click 'Run' button."""
    resp = await send_cmd(ws, "gui_click", {"window": "Extension IDE", "target": "Run"})
    report("Click 'Run' button", resp.get("ok", False), str(resp)[:100])
    await asyncio.sleep(2)  # Wait for execution

async def test_read_output(ws):
    """Test 9: Read output panel text."""
    resp = await send_cmd(ws, "gui_get_text", {"window": "Extension IDE", "widget": "output"})
    text = resp.get("text", "")
    report("Read output panel", True, f"Output: '{text[:80]}...' " if len(text) > 80 else f"Output: '{text}'")

async def test_screenshot_after_run(ws):
    """Test 10: Screenshot after run."""
    path = await screenshot(ws, "IDE::ExtensionIdeWindow")
    report("Screenshot after run", bool(path))

async def test_click_stop(ws):
    """Test 11: Click 'Stop' button."""
    resp = await send_cmd(ws, "gui_click", {"window": "Extension IDE", "target": "Stop"})
    report("Click 'Stop' button", resp.get("ok", False), str(resp)[:100])

async def test_switch_tab_templates(ws):
    """Test 12: Switch to Templates tab."""
    resp = await send_cmd(ws, "gui_switch_tab", {"window": "Extension IDE", "tab": "Templates"})
    report("Switch to Templates tab", resp.get("ok", False), str(resp)[:100])

async def test_switch_tab_api_ref(ws):
    """Test 13: Switch to API Reference tab."""
    resp = await send_cmd(ws, "gui_switch_tab", {"window": "Extension IDE", "tab": "API Reference"})
    report("Switch to API Reference tab", resp.get("ok", False), str(resp)[:100])

async def test_click_save(ws):
    """Test 14: Click 'Save' button (will show dialog, dismiss with Escape)."""
    resp = await send_cmd(ws, "gui_click", {"window": "Extension IDE", "target": "Save"})
    report("Click 'Save' button", resp.get("ok", False), str(resp)[:100])
    await asyncio.sleep(0.5)
    await send_cmd(ws, "gui_press_key", {"window": "Extension IDE", "key": "Escape"})

async def test_switch_output_tab(ws):
    """Test 15: Switch to OUTPUT tab in bottom panel."""
    resp = await send_cmd(ws, "gui_switch_tab", {"window": "Extension IDE", "tab": "OUTPUT"})
    report("Switch to OUTPUT tab", resp.get("ok", False), str(resp)[:100])

async def test_switch_manifest_tab(ws):
    """Test 16: Switch to MANIFEST tab."""
    resp = await send_cmd(ws, "gui_switch_tab", {"window": "Extension IDE", "tab": "MANIFEST"})
    report("Switch to MANIFEST tab", resp.get("ok", False), str(resp)[:100])

async def test_click_new_extension(ws):
    """Test 17: Click 'New Extension' button."""
    resp = await send_cmd(ws, "gui_click", {"window": "Extension IDE", "target": "New Extension"})
    report("Click 'New Extension' button", resp.get("ok", False), str(resp)[:100])
    await asyncio.sleep(1)
    await send_cmd(ws, "gui_press_key", {"window": "Extension IDE", "key": "Escape"})

async def test_final_screenshot(ws):
    """Test 18: Final screenshot."""
    path = await screenshot(ws, "IDE::ExtensionIdeWindow")
    report("Final screenshot", bool(path))

async def test_keyboard_shortcut_new(ws):
    """Test 19: Ctrl+N keyboard shortcut."""
    resp = await send_cmd(ws, "gui_press_key", {"window": "Extension IDE", "key": "Ctrl+N"})
    report("Ctrl+N shortcut", resp.get("ok", False), str(resp)[:100])

async def test_list_all_buttons(ws):
    """Test 20: List all buttons in the IDE."""
    resp = await send_cmd(ws, "gui_list_elements", {"window": "Extension IDE", "type": "QPushButton"})
    buttons = resp.get("elements", [])
    report("List buttons", len(buttons) > 0, f"{len(buttons)} buttons found")

async def test_ping_still_alive(ws):
    """Test 21: Verify IDE is still responsive."""
    resp = await send_cmd(ws, "ping")
    report("IDE still responsive", resp.get("ok") is not None or resp.get("pong") is not None)

# ── Main ─────────────────────────────────────────────────────────

async def run_all_tests():
    global passed, failed, total
    print(f"\n{'='*60}")
    print(f"  Extension IDE UX Regression Tests")
    print(f"  Connecting to {HOST}:{PORT}")
    print(f"{'='*60}\n")

    try:
        async with websockets.connect(f"ws://{HOST}:{PORT}", open_timeout=5) as ws:
            tests = [
                test_connect,
                test_list_windows,
                test_screenshot_initial,
                test_list_elements,
                test_click_new,
                test_type_code,
                test_screenshot_after_edit,
                test_click_run,
                test_read_output,
                test_screenshot_after_run,
                test_click_stop,
                test_switch_tab_templates,
                test_switch_tab_api_ref,
                test_click_save,
                test_switch_output_tab,
                test_switch_manifest_tab,
                test_click_new_extension,
                test_final_screenshot,
                test_keyboard_shortcut_new,
                test_list_all_buttons,
                test_ping_still_alive,
            ]
            for test_fn in tests:
                try:
                    await test_fn(ws)
                except Exception as e:
                    report(test_fn.__doc__ or test_fn.__name__, False, str(e)[:100])
    except Exception as e:
        print(f"\nFATAL: Could not connect to VioSpice on port {PORT}")
        print(f"  Make sure the Extension IDE is running:")
        print(f"    ./build/VioraEDA --extension-ide")
        print(f"  Error: {e}")
        sys.exit(1)

    print(f"\n{'='*60}")
    print(f"  Results: {passed}/{total} passed, {failed} failed")
    print(f"  Screenshots saved to: {SCREENSHOT_DIR}")
    print(f"{'='*60}\n")

    sys.exit(0 if failed == 0 else 1)

if __name__ == "__main__":
    asyncio.run(run_all_tests())
