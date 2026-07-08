#!/usr/bin/env python3
# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0
"""
Extension management UX simulation for the Extension IDE.
Tests extension creation, editing, and manifest management.

Usage:
    1. Launch: ./build/VioraEDA --extension-ide
    2. Run:    python3 extension_ide/scripts/simulate_extensions.py
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
SCREENSHOT_DIR = Path("/tmp/ux_sim_extensions")
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

TEST_EXTENSION_ID = "ux-test-extension"


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
    path = await screenshot(ws, "ext_01_initial")
    report("Initial screenshot", bool(path))


async def test_click_new_extension(ws):
    """Click New Extension button to open the scaffold dialog."""
    resp = await send_cmd(ws, "gui_click", {
        "window": WINDOW, "target": "New Extension",
    })
    await asyncio.sleep(1.0)
    await screenshot(ws, "ext_02_new_extension_dialog")
    report("Click New Extension", resp.get("ok", False), str(resp)[:120])
    return resp.get("ok", False)


async def test_fill_extension_name(ws):
    """Fill in the extension name field."""
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "name",
        "text": "UX Test Extension",
        "append": False,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "ext_03_fill_name")
    report("Fill extension name", resp.get("ok", False))


async def test_fill_extension_id(ws):
    """Fill in the extension ID field."""
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "id",
        "text": TEST_EXTENSION_ID,
        "append": False,
    })
    await asyncio.sleep(0.3)
    report("Fill extension ID", resp.get("ok", False))


async def test_fill_extension_author(ws):
    """Fill in the author field."""
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "author",
        "text": "UX Test Suite",
        "append": False,
    })
    await asyncio.sleep(0.3)
    report("Fill author field", resp.get("ok", False))


async def test_fill_extension_version(ws):
    """Fill in the version field."""
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "version",
        "text": "1.0.0",
        "append": False,
    })
    await asyncio.sleep(0.3)
    report("Fill version field", resp.get("ok", False))


async def test_fill_extension_description(ws):
    """Fill in the description field."""
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "description",
        "text": "A test extension for UX simulation",
        "append": False,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "ext_04_fill_form")
    report("Fill description field", resp.get("ok", False))


async def test_scaffold_next_step(ws):
    """Click Next to advance to template selection."""
    resp = await send_cmd(ws, "gui_click", {
        "window": WINDOW, "target": "Next",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_05_template_step")
    report("Scaffold: Next step", resp.get("ok", False), str(resp)[:120])


async def test_select_template_empty(ws):
    """Select Empty template from the combo box."""
    resp = await send_cmd(ws, "gui_click", {
        "window": WINDOW, "target": "Empty",
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "ext_06_template_empty")
    report("Select Empty template", resp.get("ok", False))


async def test_select_template_panel(ws):
    """Select Panel template from the combo box."""
    resp = await send_cmd(ws, "gui_click", {
        "window": WINDOW, "target": "Panel",
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "ext_07_template_panel")
    report("Select Panel template", resp.get("ok", False))


async def test_scaffold_next_to_confirm(ws):
    """Click Next again to advance to confirmation."""
    resp = await send_cmd(ws, "gui_click", {
        "window": WINDOW, "target": "Next",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_08_confirm_step")
    report("Scaffold: Confirm step", resp.get("ok", False))


async def test_scaffold_create(ws):
    """Click Create to scaffold the extension."""
    resp = await send_cmd(ws, "gui_click", {
        "window": WINDOW, "target": "Create",
    })
    await asyncio.sleep(1.5)
    await screenshot(ws, "ext_09_extension_created")
    report("Scaffold: Create extension", resp.get("ok", False), str(resp)[:120])


async def test_extension_in_file_tree(ws):
    """Verify the extension appears in the file tree."""
    # Switch to Explorer sidebar to see file tree
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Explorer"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_10_file_tree")
    report("Extension in file tree", resp.get("ok", False))


async def test_open_extension_directory(ws):
    """Open the created extension directory."""
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "file_tree_filter",
        "text": TEST_EXTENSION_ID,
        "append": False,
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_11_filter_extension")
    report("Filter file tree for extension", resp.get("ok", False))

    # Clear filter
    await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "file_tree_filter",
        "text": "",
        "append": False,
    })
    await asyncio.sleep(0.3)


async def test_open_main_flux(ws):
    """Open main.flux file in the editor."""
    resp = await send_cmd(ws, "gui_click", {
        "window": WINDOW, "target": "main.flux",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_12_open_main_flux")
    report("Open main.flux", resp.get("ok", False))


async def test_edit_main_flux(ws):
    """Edit the main.flux file."""
    code = (
        "def main() {\n"
        '    viora_flux_print("UX test extension loaded")\n'
        "}\n"
    )
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "editor",
        "text": code,
        "append": False,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "ext_13_edit_main_flux")
    report("Edit main.flux", resp.get("ok", False))


async def test_open_manifest_tab(ws):
    """Switch to MANIFEST tab."""
    resp = await send_cmd(ws, "gui_switch_tab", {
        "window": WINDOW, "tab": "MANIFEST",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_14_manifest_tab")
    report("Open MANIFEST tab", resp.get("ok", False))


async def test_edit_manifest(ws):
    """Edit the manifest.json content."""
    manifest = json.dumps({
        "id": TEST_EXTENSION_ID,
        "name": "UX Test Extension",
        "version": "1.0.0",
        "author": "UX Test Suite",
        "description": "A test extension for UX simulation",
        "main": "main.flux",
        "hooks": {"onActivate": "init"},
    }, indent=2)
    resp = await send_cmd(ws, "gui_type", {
        "window": WINDOW,
        "target": "manifest_editor",
        "text": manifest,
        "append": False,
    })
    await asyncio.sleep(0.3)
    await screenshot(ws, "ext_15_edit_manifest")
    report("Edit manifest.json", resp.get("ok", False))


async def test_save_extension(ws):
    """Save all files in the extension."""
    resp = await send_cmd(ws, "gui_press_key", {
        "window": WINDOW, "key": "Ctrl+S",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_16_save")
    report("Save extension files", resp.get("ok", False))
    # Dismiss any dialog
    await send_cmd(ws, "gui_press_key", {"window": WINDOW, "key": "Escape"})
    await asyncio.sleep(0.3)


async def test_run_extension(ws):
    """Run the created extension."""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Run"})
    await asyncio.sleep(2.0)
    await screenshot(ws, "ext_17_run")
    report("Run extension", resp.get("ok", False))


async def test_read_output(ws):
    """Read output after running extension."""
    resp = await send_cmd(ws, "gui_get_text", {"window": WINDOW, "widget": "output"})
    text = resp.get("text", "")
    report("Read extension output", len(text) > 0,
           f"Output: '{text[:80]}'" if text else "empty")


async def test_stop_extension(ws):
    """Stop the running extension."""
    resp = await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Stop"})
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_18_stop")
    report("Stop extension", resp.get("ok", False))


async def test_api_reference_tab(ws):
    """Switch to API Reference tab."""
    resp = await send_cmd(ws, "gui_switch_tab", {
        "window": WINDOW, "tab": "API Reference",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_19_api_reference")
    report("API Reference tab", resp.get("ok", False))


async def test_templates_tab(ws):
    """Switch to Templates tab."""
    resp = await send_cmd(ws, "gui_switch_tab", {
        "window": WINDOW, "tab": "Templates",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_20_templates")
    report("Templates tab", resp.get("ok", False))


async def test_click_manifest_button(ws):
    """Click Manifest toolbar button."""
    resp = await send_cmd(ws, "gui_click", {
        "window": WINDOW, "target": "Manifest",
    })
    await asyncio.sleep(0.5)
    await screenshot(ws, "ext_21_manifest_toolbar")
    report("Manifest toolbar button", resp.get("ok", False))


async def test_create_second_extension(ws):
    """Create a second extension to test multiple extensions."""
    resp = await send_cmd(ws, "gui_click", {
        "window": WINDOW, "target": "New Extension",
    })
    await asyncio.sleep(1.0)

    # Fill form
    await send_cmd(ws, "gui_type", {
        "window": WINDOW, "target": "name",
        "text": "Second Test Extension", "append": False,
    })
    await asyncio.sleep(0.2)
    await send_cmd(ws, "gui_type", {
        "window": WINDOW, "target": "id",
        "text": "second-ux-test", "append": False,
    })
    await asyncio.sleep(0.2)
    await send_cmd(ws, "gui_type", {
        "window": WINDOW, "target": "author",
        "text": "UX Test Suite", "append": False,
    })
    await asyncio.sleep(0.2)

    # Advance through wizard
    await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Next"})
    await asyncio.sleep(0.5)
    await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Next"})
    await asyncio.sleep(0.5)
    await send_cmd(ws, "gui_click", {"window": WINDOW, "target": "Create"})
    await asyncio.sleep(1.0)

    await screenshot(ws, "ext_22_second_extension")
    report("Create second extension", resp.get("ok", False))


async def test_final_screenshot(ws):
    path = await screenshot(ws, "ext_23_final")
    report("Final screenshot", bool(path))


async def test_still_alive(ws):
    resp = await send_cmd(ws, "ping")
    report("IDE responsive", resp.get("pong") or resp.get("ok"))


# ── Main ──────────────────────────────────────────────────────────

async def run_all_tests():
    global passed, failed, total

    print(f"\n{BOLD}{'=' * 64}{RESET}")
    print(f"{BOLD}  Extension IDE — Extension Management Simulation{RESET}")
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

            # ── Extension creation wizard ──
            print(f"\n{CYAN}── Extension Creation Wizard ──{RESET}")
            opened = await test_click_new_extension(ws)
            if opened:
                await test_fill_extension_name(ws)
                await test_fill_extension_id(ws)
                await test_fill_extension_author(ws)
                await test_fill_extension_version(ws)
                await test_fill_extension_description(ws)
                await test_scaffold_next_step(ws)

                # Template selection
                await test_select_template_empty(ws)
                await test_select_template_panel(ws)
                await test_scaffold_next_to_confirm(ws)

                # Create
                await test_scaffold_create(ws)
                await test_extension_in_file_tree(ws)

            # ── Extension file management ──
            print(f"\n{CYAN}── File Management ──{RESET}")
            await test_open_extension_directory(ws)
            await test_open_main_flux(ws)
            await test_edit_main_flux(ws)
            await test_save_extension(ws)

            # ── Manifest ──
            print(f"\n{CYAN}── Manifest Management ──{RESET}")
            await test_open_manifest_tab(ws)
            await test_edit_manifest(ws)
            await test_click_manifest_button(ws)

            # ── Run/Stop ──
            print(f"\n{CYAN}── Run / Stop ──{RESET}")
            await test_run_extension(ws)
            await test_read_output(ws)
            await test_stop_extension(ws)

            # ── Panel navigation ──
            print(f"\n{CYAN}── Panel Navigation ──{RESET}")
            await test_api_reference_tab(ws)
            await test_templates_tab(ws)

            # ── Second extension ──
            print(f"\n{CYAN}── Second Extension ──{RESET}")
            await test_create_second_extension(ws)

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
