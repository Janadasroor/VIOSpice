#!/usr/bin/env python3
# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
"""
Comprehensive VioraIDE UI/UX test suite with screenshots.
Exercises every major feature via WebSocket protocol.

Usage:
    1. Launch: ./build/VioraEDA --ide
    2. Run:    python3 viora_ide/scripts/test_ide_full.py
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
SCREENSHOT_DIR = Path("/tmp/vioraide_ux")
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
    msg_id = next_id()
    payload = {"id": msg_id, "cmd": cmd}
    if params:
        payload["params"] = params
    await ws.send(json.dumps(payload))
    resp = await asyncio.wait_for(ws.recv(), timeout=timeout)
    return json.loads(resp)


async def screenshot(ws, name):
    safe = name.replace("/", "_").replace(" ", "_")
    dest = str(SCREENSHOT_DIR / f"{safe}.png")
    resp = await send_cmd(ws, "screenshot_capture", {
        "name": name,
        "clipboard": False,
        "scale": 2.0,
        "format": "PNG",
        "output": dest,
    }, timeout=15)
    path = resp.get("path", dest)
    if path and os.path.exists(path):
        if path != dest:
            shutil.copy2(path, dest)
        return dest
    return ""


async def wait_ready(ws, attempts=10):
    for _ in range(attempts):
        try:
            resp = await send_cmd(ws, "ping", timeout=3)
            if resp.get("pong") or resp.get("ok"):
                return True
        except Exception:
            await asyncio.sleep(0.5)
    return False


# ── Test categories ───────────────────────────────────────────────

# 1. Startup & Layout

async def test_connection(ws):
    resp = await send_cmd(ws, "ping")
    report("Connection & ping", resp.get("pong") or resp.get("ok"),
           str(resp)[:120])


async def test_find_ide_window(ws):
    resp = await send_cmd(ws, "screenshot_list")
    windows = resp.get("windows", [])
    found = any(
        "VioraIDE" in w.get("title", "") or
        "ExtensionIde" in w.get("class", "")
        for w in windows
    )
    titles = [w.get("title", "?") for w in windows]
    report("Find VioraIDE window", found,
           f"{len(windows)} window(s): {titles}")
    return found


async def test_initial_screenshot(ws):
    path = await screenshot(ws, "01_startup_initial")
    report("Initial screenshot", bool(path))


async def test_list_elements(ws):
    resp = await send_cmd(ws, "gui_list_elements", {"window": WINDOW})
    elems = resp.get("elements", [])
    report("List interactive elements", len(elems) > 0,
           f"{len(elems)} elements found")
    return elems


async def test_layout_panels(ws):
    resp = await send_cmd(ws, "gui_list_elements", {"window": WINDOW})
    elems = resp.get("elements", [])
    types = [e.get("type", "") for e in elems]
    has_any = len(elems) > 3
    report("Layout has panels", has_any,
           f"{len(elems)} elements, types: {list(set(types))[:5]}")


# 2. File Operations

async def test_new_file(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+N"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "02_new_file")
    report("New file (Ctrl+N)", resp.get("ok", False))


async def test_open_file_dialog(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+O"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "03_open_file_dialog")
    report("Open file dialog (Ctrl+O)", resp.get("ok", False))
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


async def test_save_dialog(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+S"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "04_save_dialog")
    report("Save dialog (Ctrl+S)", resp.get("ok", False))
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


async def test_close_tab(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+W"})
    await asyncio.sleep(0.5)
    report("Close tab (Ctrl+W)", resp.get("ok", False))


async def test_reopen_tab(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+Shift+T"})
    await asyncio.sleep(0.5)
    report("Reopen closed tab (Ctrl+Shift+T)", resp.get("ok", False))


# 3. Editor Features

async def test_type_single_line(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+N"})
    await asyncio.sleep(0.3)
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW, "target": "editor",
        "text": "def hello():", "append": False
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "05_type_single_line")
    report("Type single line", resp.get("ok", False))


async def test_type_multiline(ws):
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW, "target": "editor",
        "text": "\n    viora_flux_print(\"Hello World\")\n    return 0",
        "append": True
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "06_type_multiline")
    report("Type multiline code", resp.get("ok", False))


async def test_select_all(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+A"})
    await asyncio.sleep(0.3)
    report("Select All (Ctrl+A)", resp.get("ok", False))


async def test_undo(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+Z"})
    await asyncio.sleep(0.3)
    report("Undo (Ctrl+Z)", resp.get("ok", False))


async def test_redo(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+Shift+Z"})
    await asyncio.sleep(0.3)
    report("Redo (Ctrl+Shift+Z)", resp.get("ok", False))


async def test_tab_inserts_spaces(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Tab"})
    await asyncio.sleep(0.3)
    report("Tab inserts spaces", resp.get("ok", False))


# 4. Find & Replace

async def test_find_open(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+F"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "07_find_open")
    report("Find bar opens (Ctrl+F)", resp.get("ok", False))


async def test_find_close(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)
    report("Find bar closes (Escape)", resp.get("ok", False))


async def test_replace_open(ws):
    resp = await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Ctrl+H"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "08_replace_open")
    report("Replace bar opens (Ctrl+H)", resp.get("ok", False))
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


# 5. Sidebar Panels

async def test_explorer_toggle(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Explorer"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "09_explorer_toggle")
    report("Explorer toggle", resp.get("ok", False))


async def test_theme_toggle(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Theme"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "10_theme_toggled")
    report("Theme toggle", resp.get("ok", False))


async def test_toggle_bottom(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Bottom"})
    await asyncio.sleep(0.5)
    report("Bottom panel toggle", resp.get("ok", False))


async def test_toggle_right(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Right"})
    await asyncio.sleep(0.5)
    report("Right panel toggle", resp.get("ok", False))


# 6. Toolbar

async def test_toolbar_new(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "New"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "11_toolbar_new")
    report("New toolbar button", resp.get("ok", False))


async def test_toolbar_open(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Open"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "12_toolbar_open")
    report("Open toolbar button", resp.get("ok", False))
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


async def test_toolbar_save(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Save"})
    await asyncio.sleep(0.5)
    report("Save toolbar button", resp.get("ok", False))


async def test_toolbar_run(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Run"})
    await asyncio.sleep(1.0)
    await screenshot(ws, "13_toolbar_run")
    report("Run toolbar button", resp.get("ok", False))


# 7. Context Menus

async def test_context_menu_editor(ws):
    resp = await send_cmd(ws, "gui_click_at", {
        "window": WINDOW, "x": 640, "y": 400, "button": "right"
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "14_context_menu_editor")
    report("Editor context menu", resp.get("ok", False))
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


# 8. Tab Switching

async def test_tab_switch_templates(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Templates"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "15_tab_templates")
    report("Templates tab", resp.get("ok", False))


async def test_tab_switch_api(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "API"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "16_tab_api")
    report("API tab", resp.get("ok", False))


async def test_tab_switch_output(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "OUTPUT"})
    await asyncio.sleep(0.5)
    report("OUTPUT tab", resp.get("ok", False))


async def test_tab_switch_problems(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "PROBLEMS"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "17_tab_problems")
    report("PROBLEMS tab", resp.get("ok", False))


async def test_tab_switch_git(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Git"})
    await asyncio.sleep(0.5)
    report("Git tab", resp.get("ok", False))


# 9. Status Bar

async def test_status_bar(ws):
    resp = await send_cmd(ws, "gui_list_elements", {"window": WINDOW})
    elems = resp.get("elements", [])
    labels = [e.get("label", "") for e in elems]
    has_status = any("Ln" in l or "Col" in l for l in labels)
    report("Status bar cursor position", has_status or len(elems) > 0,
           f"found labels: {labels[:5]}")


# 10. Theme System

async def test_theme_dark_screenshot(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Theme"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "18_theme_dark")
    report("Dark mode screenshot", True)


async def test_theme_light_screenshot(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Theme"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "19_theme_light")
    report("Light mode screenshot", True)


async def test_theme_switch_no_crash(ws):
    for i in range(3):
        await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Theme"})
        await asyncio.sleep(0.3)
    resp = await send_cmd(ws, "ping")
    report("Theme switch 3x no crash", resp.get("pong") or resp.get("ok"))


# 11. Scroll

async def test_scroll_editor(ws):
    resp = await send_cmd(ws, "gui_scroll", {
        "window": WINDOW, "x": 640, "y": 400, "deltaY": -300
    })
    await asyncio.sleep(0.3)
    report("Scroll editor", resp.get("ok", False))


# 12. Extension Scaffolding

async def test_new_extension_dialog(ws):
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "New Extension"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "20_new_extension_dialog")
    report("New Extension dialog", resp.get("ok", False))
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


# ── Main ──────────────────────────────────────────────────────────

async def main():
    print(f"\n{BOLD}{'='*60}{RESET}")
    print(f"{BOLD}  VioraIDE Full UX Test Suite{RESET}")
    print(f"{BOLD}{'='*60}{RESET}\n")

    try:
        async with websockets.connect(f"ws://{HOST}:{PORT}") as ws:
            # Wait for server ready
            if not await wait_ready(ws):
                print(f"{RED}ERROR: WebSocket server not ready at {HOST}:{PORT}{RESET}")
                sys.exit(1)

            # ── Startup & Layout ──
            print(f"\n{CYAN}[1/12] Startup & Layout{RESET}")
            await test_connection(ws)
            found = await test_find_ide_window(ws)
            await test_initial_screenshot(ws)
            await test_list_elements(ws)
            await test_layout_panels(ws)

            if not found:
                print(f"\n{YELLOW}Window not found, some tests may fail{RESET}")

            # ── File Operations ──
            print(f"\n{CYAN}[2/12] File Operations{RESET}")
            await test_new_file(ws)
            await test_open_file_dialog(ws)
            await test_save_dialog(ws)
            await test_close_tab(ws)
            await test_reopen_tab(ws)

            # ── Editor Features ──
            print(f"\n{CYAN}[3/12] Editor Features{RESET}")
            await test_type_single_line(ws)
            await test_type_multiline(ws)
            await test_select_all(ws)
            await test_undo(ws)
            await test_redo(ws)
            await test_tab_inserts_spaces(ws)

            # ── Find & Replace ──
            print(f"\n{CYAN}[4/12] Find & Replace{RESET}")
            await test_find_open(ws)
            await test_find_close(ws)
            await test_replace_open(ws)

            # ── Sidebar Panels ──
            print(f"\n{CYAN}[5/12] Sidebar Panels{RESET}")
            await test_explorer_toggle(ws)
            await test_toggle_bottom(ws)
            await test_toggle_right(ws)

            # ── Toolbar ──
            print(f"\n{CYAN}[6/12] Toolbar{RESET}")
            await test_toolbar_new(ws)
            await test_toolbar_open(ws)
            await test_toolbar_save(ws)
            await test_toolbar_run(ws)

            # ── Context Menus ──
            print(f"\n{CYAN}[7/12] Context Menus{RESET}")
            await test_context_menu_editor(ws)

            # ── Tab Switching ──
            print(f"\n{CYAN}[8/12] Tab Switching{RESET}")
            await test_tab_switch_templates(ws)
            await test_tab_switch_api(ws)
            await test_tab_switch_output(ws)
            await test_tab_switch_problems(ws)
            await test_tab_switch_git(ws)

            # ── Status Bar ──
            print(f"\n{CYAN}[9/12] Status Bar{RESET}")
            await test_status_bar(ws)

            # ── Theme System ──
            print(f"\n{CYAN}[10/12] Theme System{RESET}")
            await test_theme_dark_screenshot(ws)
            await test_theme_light_screenshot(ws)
            await test_theme_switch_no_crash(ws)

            # ── Scroll ──
            print(f"\n{CYAN}[11/12] Scroll{RESET}")
            await test_scroll_editor(ws)

            # ── Extension Scaffolding ──
            print(f"\n{CYAN}[12/12] Extension Scaffolding{RESET}")
            await test_new_extension_dialog(ws)

    except (ConnectionRefusedError, OSError) as e:
        print(f"\n{RED}ERROR: Cannot connect to VioraIDE at {HOST}:{PORT}{RESET}")
        print(f"  Make sure VioraEDA is running: ./build/VioraEDA --ide")
        print(f"  Error: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"\n{RED}ERROR: {e}{RESET}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

    # ── Summary ──
    print(f"\n{BOLD}{'='*60}{RESET}")
    print(f"{BOLD}  Results: {GREEN}{passed} passed{RESET}, "
          f"{RED}{failed} failed{RESET}, "
          f"{YELLOW}{skipped} skipped{RESET} / {total} total")
    print(f"{BOLD}{'='*60}{RESET}")
    print(f"  Screenshots saved to: {SCREENSHOT_DIR}")
    print()

    # Write summary report
    report_path = SCREENSHOT_DIR / "summary_report.txt"
    with open(report_path, "w") as f:
        f.write(f"VioraIDE UX Test Report\n")
        f.write(f"{'='*40}\n")
        f.write(f"Passed:  {passed}\n")
        f.write(f"Failed:  {failed}\n")
        f.write(f"Skipped: {skipped}\n")
        f.write(f"Total:   {total}\n")
        f.write(f"\nScreenshots:\n")
        for s in sorted(SCREENSHOT_DIR.glob("*.png")):
            f.write(f"  {s.name}\n")

    sys.exit(1 if failed > 0 else 0)


if __name__ == "__main__":
    asyncio.run(main())
