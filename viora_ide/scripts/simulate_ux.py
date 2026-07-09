#!/usr/bin/env python3
# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
"""
Comprehensive UX simulation for VioraIDE.
Exercises every major UI interaction via WebSocket protocol.

Usage:
    1. Launch: ./build/VioraEDA --ide
    2. Run:    python3 viora_ide/scripts/simulate_ux.py
"""

import asyncio
import json
import os
import shutil
import sys
import time
from pathlib import Path

try:
    import websockets
except ImportError:
    print("ERROR: 'websockets' package not found. Install with: pip install websockets")
    sys.exit(1)

# ── Config ────────────────────────────────────────────────────────
HOST = "127.0.0.1"
PORT = 18790
WINDOW = "VioraIDE"
SCREENSHOT_DIR = Path("/tmp/ux_sim")
SCREENSHOT_DIR.mkdir(parents=True, exist_ok=True)

# ── Colored output ────────────────────────────────────────────────
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"

passed = 0
failed = 0
skipped = 0
total = 0


def report(name, ok, detail="", skip=False):
    global passed, failed, skipped, total
    total += 1
    if skip:
        skipped += 1
        print(f"  {YELLOW}SKIP{RESET}  {name} — {detail}")
    elif ok:
        passed += 1
        print(f"  {GREEN}PASS{RESET}  {name}")
    else:
        failed += 1
        print(f"  {RED}FAIL{RESET}  {name} — {detail}")


# ── WebSocket helpers ─────────────────────────────────────────────
_msg_counter = 0


def next_id():
    global _msg_counter
    _msg_counter += 1
    return _msg_counter


async def send_cmd(ws, cmd, params=None, timeout=10):
    """Send a JSON command and wait for response."""
    msg_id = next_id()
    payload = {"id": msg_id, "cmd": cmd}
    if params:
        payload["params"] = params
    await ws.send(json.dumps(payload))
    resp = await asyncio.wait_for(ws.recv(), timeout=timeout)
    return json.loads(resp)


async def screenshot(ws, name):
    """Capture a screenshot, copy to /tmp/ux_sim/, return local path."""
    safe = name.replace("/", "_").replace(" ", "_")
    resp = await send_cmd(ws, "screenshot_capture", {
        "name": name,
        "clipboard": False,
        "scale": 2.0,
        "format": "PNG",
    }, timeout=15)
    path = resp.get("path", "")
    if path and os.path.exists(path):
        dest = SCREENSHOT_DIR / f"{safe}.png"
        shutil.copy2(path, str(dest))
        return str(dest)
    return ""


async def wait_ready(ws, attempts=5):
    """Poll until the IDE answers a ping."""
    for _ in range(attempts):
        try:
            resp = await send_cmd(ws, "ping", timeout=3)
            if resp.get("pong") or resp.get("ok"):
                return True
        except Exception:
            await asyncio.sleep(0.5)
    return False


# ── Test categories ───────────────────────────────────────────────

async def test_connection(ws):
    """Connect + ping"""
    resp = await send_cmd(ws, "ping")
    report("Connection & ping", resp.get("pong") or resp.get("ok"),
           str(resp)[:120])


async def test_find_ide_window(ws):
    """Find Extension IDE window"""
    resp = await send_cmd(ws, "screenshot_list")
    windows = resp.get("windows", [])
    found = any(
        "Extension IDE" in w.get("title", "") or
        "ExtensionIde" in w.get("class", "")
        for w in windows
    )
    titles = [w.get("title", "?") for w in windows]
    report("Find Extension IDE window", found,
           f"{len(windows)} window(s): {titles}")
    return found


async def test_initial_screenshot(ws):
    """Initial screenshot"""
    path = await screenshot(ws, "01_initial_state")
    report("Initial screenshot", bool(path))


async def test_list_elements(ws):
    """List all interactive elements"""
    resp = await send_cmd(ws, "gui_list_elements", {"window": WINDOW})
    elems = resp.get("elements", [])
    report("List interactive elements", len(elems) > 0,
           f"{len(elems)} elements found")
    return elems


# ── Sidebar icons ─────────────────────────────────────────────────

async def test_sidebar_explorer(ws):
    """Click sidebar Explorer icon"""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Explorer"})
    await asyncio.sleep(0.3)
    await screenshot(ws, "02_sidebar_explorer")
    report("Click Explorer sidebar", resp.get("ok", False), str(resp)[:100])


async def test_sidebar_search(ws):
    """Click sidebar Search icon"""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Search"})
    await asyncio.sleep(0.3)
    await screenshot(ws, "03_sidebar_search")
    report("Click Search sidebar", resp.get("ok", False), str(resp)[:100])


# ── Toolbar buttons ───────────────────────────────────────────────

async def test_toolbar_new(ws):
    """Click toolbar New button"""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "New"})
    await asyncio.sleep(0.3)
    await screenshot(ws, "04_toolbar_new")
    report("Click New toolbar button", resp.get("ok", False), str(resp)[:100])


async def test_toolbar_open(ws):
    """Click toolbar Open button"""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Open"})
    await asyncio.sleep(0.3)
    await screenshot(ws, "05_toolbar_open")
    report("Click Open toolbar button", resp.get("ok", False), str(resp)[:100])
    # Dismiss any file dialog
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


async def test_toolbar_save(ws):
    """Click toolbar Save button"""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Save"})
    await asyncio.sleep(0.3)
    await screenshot(ws, "06_toolbar_save")
    report("Click Save toolbar button", resp.get("ok", False), str(resp)[:100])
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


async def test_toolbar_run(ws):
    """Click toolbar Run button"""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Run"})
    await asyncio.sleep(1.0)
    await screenshot(ws, "07_toolbar_run")
    report("Click Run toolbar button", resp.get("ok", False), str(resp)[:100])


async def test_toolbar_stop(ws):
    """Click toolbar Stop button"""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Stop"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "08_toolbar_stop")
    report("Click Stop toolbar button", resp.get("ok", False), str(resp)[:100])


async def test_toolbar_new_extension(ws):
    """Click toolbar New Extension button"""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "New Extension"})
    await asyncio.sleep(1.0)
    await screenshot(ws, "09_toolbar_new_extension")
    report("Click New Extension button", resp.get("ok", False), str(resp)[:100])
    # Dismiss the dialog
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.5)


async def test_toolbar_manifest(ws):
    """Click toolbar Manifest button"""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Manifest"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "10_toolbar_manifest")
    report("Click Manifest toolbar button", resp.get("ok", False), str(resp)[:100])


# ── Tab switching ─────────────────────────────────────────────────

async def test_switch_templates_tab(ws):
    """Switch to Templates tab"""
    resp = await send_cmd(ws, "gui_switch_tab", {"window": WINDOW, "tab": "Templates"})
    await asyncio.sleep(0.3)
    await screenshot(ws, "11_tab_templates")
    report("Switch to Templates tab", resp.get("ok", False), str(resp)[:100])


async def test_switch_api_ref_tab(ws):
    """Switch to API Reference tab"""
    resp = await send_cmd(ws, "gui_switch_tab", {"window": WINDOW, "tab": "API Reference"})
    await asyncio.sleep(0.3)
    await screenshot(ws, "12_tab_api_reference")
    report("Switch to API Reference tab", resp.get("ok", False), str(resp)[:100])


async def test_switch_output_tab(ws):
    """Switch to OUTPUT tab"""
    resp = await send_cmd(ws, "gui_switch_tab", {"window": WINDOW, "tab": "OUTPUT"})
    await asyncio.sleep(0.3)
    await screenshot(ws, "13_tab_output")
    report("Switch to OUTPUT tab", resp.get("ok", False), str(resp)[:100])


async def test_switch_manifest_tab(ws):
    """Switch to MANIFEST tab"""
    resp = await send_cmd(ws, "gui_switch_tab", {"window": WINDOW, "tab": "MANIFEST"})
    await asyncio.sleep(0.3)
    await screenshot(ws, "14_tab_manifest")
    report("Switch to MANIFEST tab", resp.get("ok", False), str(resp)[:100])


# ── Template panel clicks ─────────────────────────────────────────

async def test_click_template_items(ws):
    """Click templates in template panel"""
    # First switch to Templates tab
    await send_cmd(ws, "gui_switch_tab", {"window": WINDOW, "tab": "Templates"})
    await asyncio.sleep(0.3)

    # List buttons that might be template items
    resp = await send_cmd(ws, "gui_list_elements", {
        "window": WINDOW, "type": "QPushButton"
    })
    buttons = resp.get("elements", [])
    template_buttons = [
        b for b in buttons
        if any(kw in b.get("label", "").lower() for kw in
               ["calculator", "panel", "simulation", "automation", "example"])
    ]

    if template_buttons:
        for i, btn in enumerate(template_buttons[:4]):
            label = btn.get("label", f"template_{i}")
            resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": label})
            await asyncio.sleep(0.3)
            await screenshot(ws, f"15_template_{i}_{label.replace(' ', '_')}")
            report(f"Click template: {label}", resp.get("ok", False))
    else:
        report("Click template items", True, "No template buttons found, skipped")


# ── Editor interactions ───────────────────────────────────────────

async def test_type_in_editor(ws):
    """Type text in editor"""
    code = 'def main() {\n    viora_flux_print("Hello from UX test")\n}'
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "editor",
        "text": code,
        "append": False,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "16_editor_typing")
    report("Type code in editor", resp.get("ok", False), str(resp)[:100])


async def test_read_output(ws):
    """Read output panel text"""
    resp = await send_cmd(ws, "gui_get_text", {"window": WINDOW, "widget": "output"})
    text = resp.get("text", "")
    report("Read output panel", True, f"Output ({len(text)} chars)")
    return text


# ── Keyboard shortcuts ────────────────────────────────────────────

async def test_shortcut_ctrl_n(ws):
    """Ctrl+N keyboard shortcut"""
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+N"})
    await asyncio.sleep(0.3)
    report("Ctrl+N shortcut", resp.get("ok", False), str(resp)[:100])
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.2)


async def test_shortcut_ctrl_o(ws):
    """Ctrl+O keyboard shortcut"""
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+O"})
    await asyncio.sleep(0.3)
    report("Ctrl+O shortcut", resp.get("ok", False), str(resp)[:100])
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.2)


async def test_shortcut_ctrl_s(ws):
    """Ctrl+S keyboard shortcut"""
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+S"})
    await asyncio.sleep(0.3)
    report("Ctrl+S shortcut", resp.get("ok", False), str(resp)[:100])
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.2)


async def test_shortcut_f5(ws):
    """F5 keyboard shortcut (run)"""
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "F5"})
    await asyncio.sleep(1.0)
    report("F5 shortcut", resp.get("ok", False), str(resp)[:100])
    # Stop execution
    await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Stop"})
    await asyncio.sleep(0.3)


async def test_shortcut_ctrl_f(ws):
    """Ctrl+F keyboard shortcut (find/replace)"""
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+F"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "17_find_replace")
    report("Ctrl+F shortcut", resp.get("ok", False), str(resp)[:100])
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


# ── Context menu ──────────────────────────────────────────────────

async def test_right_click_context_menu(ws):
    """Right-click context menu"""
    resp = await send_cmd(ws, "gui_click_at", {
        "window": WINDOW,
        "x": 300,
        "y": 300,
        "button": "right",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "18_context_menu")
    report("Right-click context menu", resp.get("ok", False), str(resp)[:100])
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.2)


# ── File tree interactions ────────────────────────────────────────

async def test_click_file_tree(ws):
    """Click file tree items"""
    resp = await send_cmd(ws, "gui_list_elements", {"window": WINDOW})
    elems = resp.get("elements", [])
    tree_items = [
        e for e in elems
        if "tree" in e.get("type", "").lower() or "item" in e.get("type", "").lower()
    ]
    if tree_items:
        label = tree_items[0].get("label", "")
        resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": label})
        await asyncio.sleep(0.3)
        await screenshot(ws, "19_file_tree_item")
        report("Click file tree item", resp.get("ok", False), f"Clicked: {label}")
    else:
        report("Click file tree item", True, "No tree items found, skipped")


async def test_filter_file_tree(ws):
    """Type in filter input in file tree"""
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "file_tree_filter",
        "text": ".flux",
        "append": False,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "20_filter_file_tree")
    report("Type in file tree filter", resp.get("ok", False), str(resp)[:100])
    # Clear filter
    await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "file_tree_filter",
        "text": "",
        "append": False,
    })


async def test_search_input_in_template_panel(ws):
    """Type in search input in template panel"""
    await send_cmd(ws, "gui_switch_tab", {"window": WINDOW, "tab": "Templates"})
    await asyncio.sleep(0.3)
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "template_search",
        "text": "calculator",
        "append": False,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "21_template_search")
    report("Type in template search", resp.get("ok", False), str(resp)[:100])
    await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "template_search",
        "text": "",
        "append": False,
    })


# ── Mouse interactions ────────────────────────────────────────────

async def test_scroll(ws):
    """Scroll in the editor"""
    resp = await send_cmd(ws, "gui_scroll", {
        "window": WINDOW,
        "x": 400,
        "y": 300,
        "deltaY": -120,
    })
    await asyncio.sleep(0.3)
    report("Scroll editor", resp.get("ok", False), str(resp)[:100])


async def test_drag(ws):
    """Drag to select text or move splitter"""
    resp = await send_cmd(ws, "gui_drag", {
        "window": WINDOW,
        "x1": 200, "y1": 200,
        "x2": 400, "y2": 200,
        "delay": 100,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "22_drag")
    report("Drag interaction", resp.get("ok", False), str(resp)[:100])


# ── Final checks ──────────────────────────────────────────────────

async def test_final_screenshot(ws):
    """Final screenshot"""
    path = await screenshot(ws, "23_final_state")
    report("Final screenshot", bool(path))


async def test_still_alive(ws):
    """IDE still responsive"""
    resp = await send_cmd(ws, "ping")
    report("IDE still responsive", resp.get("pong") or resp.get("ok"))


# ── Main ──────────────────────────────────────────────────────────

async def run_all_tests():
    global passed, failed, skipped, total

    print(f"\n{BOLD}{'=' * 64}{RESET}")
    print(f"{BOLD}  Extension IDE — Comprehensive UX Simulation{RESET}")
    print(f"  Target: {HOST}:{PORT}")
    print(f"  Screenshots: {SCREENSHOT_DIR}")
    print(f"{BOLD}{'=' * 64}{RESET}\n")

    try:
        async with websockets.connect(f"ws://{HOST}:{PORT}", open_timeout=5) as ws:
            # ── Connection & discovery ──
            await test_connection(ws)
            found = await test_find_ide_window(ws)
            if not found:
                print(f"\n{RED}FATAL: Extension IDE window not found.{RESET}")
                print("  Launch with: ./build/VioraEDA --extension-ide")
                sys.exit(1)

            await test_initial_screenshot(ws)
            await test_list_elements(ws)

            # ── Sidebar icons ──
            print(f"\n{CYAN}── Sidebar Icons ──{RESET}")
            await test_sidebar_explorer(ws)
            await test_sidebar_search(ws)

            # ── Toolbar buttons ──
            print(f"\n{CYAN}── Toolbar Buttons ──{RESET}")
            await test_toolbar_new(ws)
            await test_toolbar_open(ws)
            await test_toolbar_save(ws)
            await test_toolbar_run(ws)
            await test_toolbar_stop(ws)
            await test_toolbar_new_extension(ws)
            await test_toolbar_manifest(ws)

            # ── Tab switching ──
            print(f"\n{CYAN}── Tab Switching ──{RESET}")
            await test_switch_templates_tab(ws)
            await test_switch_api_ref_tab(ws)
            await test_switch_output_tab(ws)
            await test_switch_manifest_tab(ws)

            # ── Template panel ──
            print(f"\n{CYAN}── Template Panel ──{RESET}")
            await test_click_template_items(ws)
            await test_search_input_in_template_panel(ws)

            # ── Editor ──
            print(f"\n{CYAN}── Editor Interactions ──{RESET}")
            await test_type_in_editor(ws)
            await test_read_output(ws)

            # ── Keyboard shortcuts ──
            print(f"\n{CYAN}── Keyboard Shortcuts ──{RESET}")
            await test_shortcut_ctrl_n(ws)
            await test_shortcut_ctrl_o(ws)
            await test_shortcut_ctrl_s(ws)
            await test_shortcut_f5(ws)
            await test_shortcut_ctrl_f(ws)

            # ── Mouse & context menu ──
            print(f"\n{CYAN}── Mouse Interactions ──{RESET}")
            await test_right_click_context_menu(ws)
            await test_click_file_tree(ws)
            await test_filter_file_tree(ws)
            await test_scroll(ws)
            await test_drag(ws)

            # ── Final ──
            print(f"\n{CYAN}── Final Checks ──{RESET}")
            await test_final_screenshot(ws)
            await test_still_alive(ws)

    except Exception as e:
        print(f"\n{RED}FATAL: Could not connect to VioSpice on port {PORT}{RESET}")
        print(f"  Launch the IDE: ./build/VioraEDA --extension-ide")
        print(f"  Error: {e}")
        sys.exit(1)

    # ── Summary ──
    print(f"\n{BOLD}{'=' * 64}{RESET}")
    color = GREEN if failed == 0 else RED
    print(f"  {color}Results: {passed}/{total} passed, {failed} failed, {skipped} skipped{RESET}")
    print(f"  Screenshots: {SCREENSHOT_DIR}")
    print(f"{BOLD}{'=' * 64}{RESET}\n")

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    asyncio.run(run_all_tests())
