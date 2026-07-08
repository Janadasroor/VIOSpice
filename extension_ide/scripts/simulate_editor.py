#!/usr/bin/env python3
# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
"""
Editor-focused UX simulation for the Extension IDE.
Tests code editing, navigation, find/replace, and run.

Usage:
    1. Launch: ./build/VioraEDA --extension-ide
    2. Run:    python3 extension_ide/scripts/simulate_editor.py
"""

import asyncio
import json
import os
import shutil
import sys
from pathlib import Path

try:
    import websockets
except ImportError:
    print("ERROR: 'websockets' package not found. Install with: pip install websockets")
    sys.exit(1)

# ── Config ────────────────────────────────────────────────────────
HOST = "127.0.0.1"
PORT = 18790
WINDOW = "Extension IDE"
SCREENSHOT_DIR = Path("/tmp/ux_sim_editor")
SCREENSHOT_DIR.mkdir(parents=True, exist_ok=True)

GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"

passed = 0
failed = 0
total = 0
_msg_counter = 0


def report(name, ok, detail=""):
    global passed, failed, total
    total += 1
    if ok:
        passed += 1
        print(f"  {GREEN}PASS{RESET}  {name}")
    else:
        failed += 1
        print(f"  {RED}FAIL{RESET}  {name} — {detail}")


def next_id():
    global _msg_counter
    _msg_counter += 1
    return _msg_counter


async def send_cmd(ws, cmd, params=None, timeout=10):
    payload = {"id": next_id(), "cmd": cmd}
    if params:
        payload["params"] = params
    await ws.send(json.dumps(payload))
    resp = await asyncio.wait_for(ws.recv(), timeout=timeout)
    return json.loads(resp)


async def screenshot(ws, name):
    safe = name.replace("/", "_").replace(" ", "_")
    resp = await send_cmd(ws, "screenshot_capture", {
        "name": name, "clipboard": False, "scale": 2.0, "format": "PNG",
    }, timeout=15)
    path = resp.get("path", "")
    if path and os.path.exists(path):
        dest = SCREENSHOT_DIR / f"{safe}.png"
        shutil.copy2(path, str(dest))
        return str(dest)
    return ""


# ── Tests ─────────────────────────────────────────────────────────

async def test_connect(ws):
    resp = await send_cmd(ws, "ping")
    report("Connection", resp.get("pong") or resp.get("ok"))


async def test_find_window(ws):
    resp = await send_cmd(ws, "screenshot_list")
    windows = resp.get("windows", [])
    found = any(
        "Extension IDE" in w.get("title", "")
        for w in windows
    )
    report("Find IDE window", found, f"{len(windows)} window(s)")
    return found


async def test_initial_screenshot(ws):
    path = await screenshot(ws, "editor_01_initial")
    report("Initial screenshot", bool(path))


async def test_new_file(ws):
    """Create a new file via toolbar button."""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "New"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "editor_02_new_file")
    report("New file (toolbar)", resp.get("ok", False))


async def test_type_multiline_code(ws):
    """Type a multi-line FluxScript program."""
    code = (
        "import std.io\n"
        "\n"
        "var counter = 0\n"
        "\n"
        "def main() {\n"
        '    viora_flux_print("Counter: " + string(counter))\n'
        "    counter = counter + 1\n"
        "}\n"
    )
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "editor",
        "text": code,
        "append": False,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "editor_03_multiline_code")
    report("Type multi-line FluxScript", resp.get("ok", False), str(resp)[:120])


async def test_type_append_mode(ws):
    """Append additional code to the editor."""
    extra = (
        "\n"
        "def on_activate() {\n"
        '    viora_flux_print("Extension activated")\n'
        "}\n"
    )
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "editor",
        "text": extra,
        "append": True,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "editor_04_append_code")
    report("Append code to editor", resp.get("ok", False))


async def test_navigate_lines(ws):
    """Navigate cursor with arrow keys and Home/End."""
    keys = [
        ("Home", "Go to line start"),
        ("Down", "Move cursor down"),
        ("Down", "Move cursor down"),
        ("End", "Go to line end"),
        ("Up", "Move cursor up"),
        ("Left", "Move cursor left"),
        ("Right", "Move cursor right"),
        ("Ctrl+Home", "Go to file start"),
        ("Ctrl+End", "Go to file end"),
    ]
    for key, desc in keys:
        resp = await send_cmd(ws, "gui_press_key", {
            "window": WINDOW, "key": key,
        })
        if not resp.get("ok", False):
            report(f"Navigate: {desc} ({key})", False, str(resp)[:80])
            return
    await asyncio.sleep(0.2)
    await screenshot(ws, "editor_05_navigation")
    report("Line navigation (arrows, Home, End)", True)


async def test_select_text(ws):
    """Select text with Shift+Arrow keys."""
    keys = [
        "Ctrl+Home",
        "Shift+Down", "Shift+Down", "Shift+End",
    ]
    for key in keys:
        resp = await send_cmd(ws, "gui_press_key", {
            "window": WINDOW, "key": key,
        })
        if not resp.get("ok", False):
            report(f"Select text ({key})", False, str(resp)[:80])
            return
    await asyncio.sleep(0.2)
    await screenshot(ws, "editor_06_selection")
    report("Select text (Shift+Arrow)", True)


async def test_delete_selection(ws):
    """Delete selected text."""
    resp = await send_cmd(ws, "gui_press_key", {
        "window": WINDOW, "key": "Backspace",
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "editor_07_delete_selection")
    report("Delete selection", resp.get("ok", False))


async def test_find_replace_open(ws):
    """Open Find/Replace with Ctrl+H."""
    resp = await send_cmd(ws, "gui_press_key", {
        "window": WINDOW, "key": "Ctrl+H",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "editor_08_find_replace")
    report("Open Find/Replace (Ctrl+H)", resp.get("ok", False))


async def test_find_replace_type(ws):
    """Type search/replace terms."""
    # Type "counter" in search field
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "find_replace_search",
        "text": "counter",
        "append": False,
    })
    await asyncio.sleep(0.3)

    # Type replacement in replace field
    resp2 = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "find_replace_replace",
        "text": "count",
        "append": False,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "editor_09_find_replace_terms")
    report("Type search/replace terms",
           resp.get("ok", False) and resp2.get("ok", False))


async def test_close_find_replace(ws):
    """Close Find/Replace."""
    resp = await send_cmd(ws, "gui_press_key", {
        "window": WINDOW, "key": "Escape",
    })
    await asyncio.sleep(0.3)
    report("Close Find/Replace", resp.get("ok", False))


async def test_save_file(ws):
    """Save the current file with Ctrl+S."""
    resp = await send_cmd(ws, "gui_press_key", {
        "window": WINDOW, "key": "Ctrl+S",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "editor_10_save")
    report("Save file (Ctrl+S)", resp.get("ok", False))
    # Dismiss any save dialog
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


async def test_open_file_dialog(ws):
    """Open file dialog and dismiss."""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Open"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "editor_11_open_dialog")
    report("Open file dialog", resp.get("ok", False))
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


async def test_run_extension(ws):
    """Run the extension with toolbar Run button."""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Run"})
    await asyncio.sleep(2.0)
    await screenshot(ws, "editor_12_run")
    report("Run extension", resp.get("ok", False))


async def test_read_output(ws):
    """Read output panel after run."""
    resp = await send_cmd(ws, "gui_get_text", {"window": WINDOW, "widget": "output"})
    text = resp.get("text", "")
    has_output = len(text) > 0
    report("Read output after run", has_output,
           f"Output: '{text[:80]}'" if text else "empty")


async def test_stop_extension(ws):
    """Stop the running extension."""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Stop"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "editor_13_stop")
    report("Stop extension", resp.get("ok", False))


async def test_run_with_f5(ws):
    """Run with F5 shortcut."""
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "F5"})
    await asyncio.sleep(2.0)
    await screenshot(ws, "editor_14_run_f5")
    report("Run with F5", resp.get("ok", False))
    await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Stop"})
    await asyncio.sleep(0.5)


async def test_undo_redo(ws):
    """Test undo (Ctrl+Z) and redo (Ctrl+Shift+Z)."""
    resp_undo = await send_cmd(ws, "gui_press_key", {
        "window": WINDOW, "key": "Ctrl+Z",
    })
    await asyncio.sleep(0.2)
    resp_redo = await send_cmd(ws, "gui_press_key", {
        "window": WINDOW, "key": "Ctrl+Shift+Z",
    })
    await asyncio.sleep(0.2)
    await screenshot(ws, "editor_15_undo_redo")
    report("Undo/Redo", resp_undo.get("ok", False) and resp_redo.get("ok", False))


async def test_select_all(ws):
    """Select all with Ctrl+A."""
    resp = await send_cmd(ws, "gui_press_key", {
        "window": WINDOW, "key": "Ctrl+A",
    })
    await asyncio.sleep(0.2)
    await screenshot(ws, "editor_16_select_all")
    report("Select all (Ctrl+A)", resp.get("ok", False))


async def test_final_screenshot(ws):
    path = await screenshot(ws, "editor_17_final")
    report("Final screenshot", bool(path))


async def test_still_alive(ws):
    resp = await send_cmd(ws, "ping")
    report("IDE responsive", resp.get("pong") or resp.get("ok"))


# ── Main ──────────────────────────────────────────────────────────

async def run_all_tests():
    global passed, failed, total

    print(f"\n{BOLD}{'=' * 64}{RESET}")
    print(f"{BOLD}  Extension IDE — Editor Simulation{RESET}")
    print(f"  Target: {HOST}:{PORT}")
    print(f"  Screenshots: {SCREENSHOT_DIR}")
    print(f"{BOLD}{'=' * 64}{RESET}\n")

    try:
        async with websockets.connect(f"ws://{HOST}:{PORT}", open_timeout=5) as ws:
            await test_connect(ws)
            found = await test_find_window(ws)
            if not found:
                print(f"\n{RED}FATAL: Extension IDE window not found.{RESET}")
                sys.exit(1)

            await test_initial_screenshot(ws)

            # ── File operations ──
            print(f"\n{CYAN}── File Operations ──{RESET}")
            await test_new_file(ws)
            await test_open_file_dialog(ws)

            # ── Code editing ──
            print(f"\n{CYAN}── Code Editing ──{RESET}")
            await test_type_multiline_code(ws)
            await test_type_append_mode(ws)

            # ── Navigation ──
            print(f"\n{CYAN}── Navigation ──{RESET}")
            await test_navigate_lines(ws)
            await test_select_text(ws)
            await test_delete_selection(ws)
            await test_select_all(ws)
            await test_undo_redo(ws)

            # ── Find/Replace ──
            print(f"\n{CYAN}── Find/Replace ──{RESET}")
            await test_find_replace_open(ws)
            await test_find_replace_type(ws)
            await test_close_find_replace(ws)

            # ── Save ──
            print(f"\n{CYAN}── Save ──{RESET}")
            await test_save_file(ws)

            # ── Run/Stop ──
            print(f"\n{CYAN}── Run / Stop ──{RESET}")
            await test_run_extension(ws)
            await test_read_output(ws)
            await test_stop_extension(ws)
            await test_run_with_f5(ws)

            # ── Final ──
            print(f"\n{CYAN}── Final Checks ──{RESET}")
            await test_final_screenshot(ws)
            await test_still_alive(ws)

    except Exception as e:
        print(f"\n{RED}FATAL: Could not connect to VioSpice on port {PORT}{RESET}")
        print(f"  Launch: ./build/VioraEDA --extension-ide")
        print(f"  Error: {e}")
        sys.exit(1)

    print(f"\n{BOLD}{'=' * 64}{RESET}")
    color = GREEN if failed == 0 else RED
    print(f"  {color}Results: {passed}/{total} passed, {failed} failed{RESET}")
    print(f"  Screenshots: {SCREENSHOT_DIR}")
    print(f"{BOLD}{'=' * 64}{RESET}\n")

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    asyncio.run(run_all_tests())
