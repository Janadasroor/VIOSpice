#!/usr/bin/env python3

# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

"""FastMCP server for VioraEDA — provides AI agents with circuit design tools."""

import importlib.util
import logging
import sys
from pathlib import Path

# Set up logging to stderr (never stdout — stdio transport uses stdout for JSON-RPC)
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    stream=sys.stderr,
)

ROOT = Path(__file__).resolve().parents[2]

# Load vspice.mcp directly (bypasses vspice/__init__.py which requires _core .so)
spec = importlib.util.spec_from_file_location(
    "core", str(ROOT / "python" / "vspice" / "mcp.py")
)
core = importlib.util.module_from_spec(spec)
sys.modules["core"] = core
spec.loader.exec_module(core)

from mcp.server.fastmcp import FastMCP, Image

mcp = FastMCP("VioraEDA")


# ── Viora Design Tools ──────────────────────────────────────────

@mcp.tool()
def viora_status() -> dict:
    """Show VioraEDA project, CLI engine, and ML API status."""
    return core.get_detailed_status()


@mcp.tool()
def viora_get_project_root() -> dict:
    """Return the project root path.  All relative paths in other tools
    are resolved against this directory."""
    return core.get_project_root()


@mcp.tool()
def viora_list_files(path: str = ".") -> dict:
    """List files and directories in the project workspace."""
    return core.list_files(path)


@mcp.tool()
def viora_read_file(path: str) -> dict:
    """Read the content of a schematic, script, or model file."""
    return core.read_file(path)


@mcp.tool()
def viora_write_file(path: str, content: str) -> dict:
    """Create or update a design file (schematic, netlist, script)."""
    return core.write_file(path, content)


@mcp.tool()
def viora_symbol_list(path: str = ".") -> dict:
    """Query the symbol library for available components."""
    return core.symbol_list(path)


@mcp.tool()
def viora_schematic_query(file: str) -> dict:
    """Analyze a .flxsch schematic to extract components and connectivity."""
    return core.schematic_query(file)


@mcp.tool()
def viora_flux_run(
    file: str = None, code: str = None,
    t: float = None, inputs: list = None
) -> dict:
    """Execute automation logic via FluxScript (file or raw code)."""
    return core.flux_run(file=file, code=code, t=t, inputs=inputs)


@mcp.tool()
def viora_flux_eval(expression: str) -> dict:
    """Evaluate a single FluxScript expression and return the result."""
    return core.flux_run(code=expression)


@mcp.tool()
def viora_flux_help() -> dict:
    """Get FluxScript syntax and API documentation."""
    help_path = core.ROOT / "docs" / "EXTENSION_API.md"
    if help_path.exists():
        return {"ok": True, "manual": help_path.read_text()}
    return {"ok": False, "error": "FluxScript manual not found."}


@mcp.tool()
def viora_pcb_render(file: str, out: str, timeout: int = 60) -> Image:
    """Render a .pcb board to a PNG image for visual inspection."""
    res = core.pcb_render(file, out, timeout=timeout)
    if not res.get("ok"):
        raise RuntimeError(res.get("error", "PCB render failed"))
    return Image(path=out)


@mcp.tool()
def viora_schematic_render(
    file: str, out: str,
    transparent: bool = False, scale: float = 4.0, timeout: int = 60
) -> Image:
    """Render a .flxsch schematic to a PNG image for visual inspection."""
    res = core.schematic_render(file, out, transparent=transparent, scale=scale, timeout=timeout)
    if not res.get("ok"):
        raise RuntimeError(res.get("error", "Schematic render failed"))
    return Image(path=out)


@mcp.tool()
def viora_symbol_render(
    file: str, out: str,
    transparent: bool = False, scale: float = 4.0, timeout: int = 60
) -> Image:
    """Render a .viosym symbol to a PNG image."""
    res = core.symbol_render(file, out, transparent=transparent, scale=scale, timeout=timeout)
    if not res.get("ok"):
        raise RuntimeError(res.get("error", "Symbol render failed"))
    return Image(path=out)


# ── VioSpice Simulation Tools ───────────────────────────────────

@mcp.tool()
def viospice_raw_info(file: str) -> dict:
    """Get signal metadata from a .raw simulation binary."""
    return core.raw_info(file)


@mcp.tool()
def viospice_raw_export(file: str, out: str, format: str = "json") -> dict:
    """Export binary simulation waveforms to JSON, CSV, or Parquet."""
    return core.raw_export(file, out, fmt=format)


@mcp.tool()
def viospice_launch_viewer(file: str, type: str = "plot") -> dict:
    """Launch the standalone visual waveform viewer (oscilloscope) for a .raw simulation file.
    Use type='osc' for a hardware-realistic analog oscilloscope view.
    """
    return core.launch_viewer(file, type=type)


@mcp.tool()
def viospice_verilog_inspect(file: str, module: str = None) -> dict:
    """Inspect a SystemVerilog file to extract module names and port definitions (inputs/outputs).
    Use this to understand how to wire an SV block into a schematic or netlist.
    """
    return core.verilog_inspect(file, module=module)


@mcp.tool()
def viora_list_examples() -> dict:
    """Discover reference schematics and automation scripts."""
    return core.list_files("examples")


@mcp.tool()
def viora_get_api_docs(topic: str = "all") -> dict:
    """Get detailed documentation for VioSpice APIs (FluxScript, Extension, or MCP)."""
    docs_path = Path("docs")
    files = {
        "flux": "FLUXSCRIPT.md",
        "extension": "EXTENSION_API.md",
        "mcp": "README_MCP.md"
    }
    
    if topic in files:
        target = docs_path / files[topic]
        if target.exists():
            return {"ok": True, "topic": topic, "content": target.read_text()}
    
    # Return index if topic not found or "all"
    return {
        "ok": True, 
        "available_topics": list(files.keys()),
        "message": "Specify a topic to get full docs."
    }


@mcp.tool()
def viospice_smart_signal_help() -> dict:
    """Get documentation and templates for creating behavioral XSPICE JIT models (Smart Signals).
    These models run FluxScript code directly inside the SPICE simulation loop at high speed.
    """
    return {
        "ok": True,
        "description": "Smart Signals allow embedding JIT-compiled behavioral logic into SPICE.",
        "template": "def update(t, inputs) {\n    # t: current simulation time (float)\n    # inputs: list of input node voltages\n    # Example: Simple Comparator with hysteresis\n    v_in = inputs[0]\n    v_ref = inputs[1]\n    return v_in > v_ref ? 5.0 : 0.0;\n}",
        "usage_example": {
            "ref": "SB1",
            "code": "def update(t, inputs) { return sin(2*pi*1000*t); }",
            "inputs": ["IN1"],
            "outputs": ["OUT"]
        }
    }


@mcp.tool()
def viospice_netlist_validate(file: str = None, cir: str = None) -> dict:
    """Pre-flight validation of a SPICE netlist or schematic."""
    return core.netlist_validate(file=file, cir=cir)


@mcp.tool()
def viospice_netlist_to_schematic(file: str, out: str = None) -> dict:
    """Convert a validated SPICE netlist (.cir) into a Viora schematic (.flxsch).
    AI Agents: Always write and test a .cir file first, then call this to generate the schematic.
    """
    return core.netlist_to_schematic(file, out)


@mcp.tool()
def viospice_netlist_run_async(
    file: str = None,
    cir: str = None,
    analysis: str = None,
    stop: str = None,
    step: str = None,
    signals: list = None,
    robust: bool = False,
    compat: bool = True,
    smart_signals: list = None,
    verilog_blocks: list = None,
    xspice_blocks: list = None,
    options: str = None,
    temperature: float = None,
):
    """Start a SPICE simulation in the background and return a job ID.
    Poll for completion with `viospice_netlist_job_status`.
    Retrieve results with `viospice_netlist_job_result`.
    """
    return core.netlist_run_async(
        file=file, cir=cir,
        analysis=analysis, stop=stop, step=step,
        signals=signals,
        robust=robust, compat=compat,
        smart_signals=smart_signals,
        verilog_blocks=verilog_blocks,
        xspice_blocks=xspice_blocks,
        options=options, temperature=temperature,
    )


@mcp.tool()
def viospice_netlist_job_status(job_id: str) -> dict:
    """Check progress of a background simulation job.
    Returns status (queued/running/done/error) and progress percentage."""
    return core.netlist_job_status(job_id)


@mcp.tool()
def viospice_netlist_job_result(job_id: str) -> dict:
    """Retrieve the result of a completed background simulation job.
    Only call this when status is 'done' or 'error'."""
    return core.netlist_job_result(job_id)


@mcp.tool()
def viospice_netlist_run(
    file: str = None,
    cir: str = None,
    analysis: str = None,
    stop: str = None,
    step: str = None,
    signals: list = None,
    robust: bool = False,
    compat: bool = True,
    smart_signals: list = None,
    verilog_blocks: list = None,
    options: str = None,
    temperature: float = None,
):  # fmt: off
    """Execute a SPICE simulation from a netlist or schematic.
    Returns parsed waveform data in JSON format.

    Parameters
    ----------
    file : .cir or .flxsch path (relative to project root, or absolute)
    cir : raw SPICE netlist text (alternative to file)
    analysis : analysis directive, e.g. 'tran 1n 100u'
    stop : transient stop time (alternative to analysis=)
    step : transient time step
    signals : list of nodes to save, e.g. ["v(1)","v(out)","i(R1)"].
              If omitted, all nodes are saved.
    robust : enable damped simulation mode for tough convergence
    compat : enable LTspice compatibility mode (default: True)
    smart_signals : list of dicts with keys:
        ref (str) - component reference, e.g. 'SB1'
        code (str) - FluxScript behavioral code
        inputs (list[str]) - input node names
        outputs (list[str]) - output node names (required)
    verilog_blocks : list of dicts with keys:
        ref (str) - component reference, e.g. 'U1'
        file (str) - path to .sv file
        code (str) - raw SystemVerilog code (alternative to file)
        module (str) - module name (default: 'top')
        inputs (list[str]) - input net names (in port order)
        outputs (list[str]) - output net names (in port order)
    options : raw SPICE option string, e.g. 'reltol=1e-4 abstol=1e-9'
    temperature : simulation temperature in Celsius
    """
    v_args = dict(
        file=file, cir=cir,
        analysis=analysis, stop=stop, step=step,
        signals=signals, json_out=True,
        robust=robust, compat=compat,
        smart_signals=smart_signals,
        verilog_blocks=verilog_blocks,
    )
    if options:
        v_args["options"] = options
    if temperature is not None:
        v_args["temperature"] = temperature
    return core.netlist_run(**v_args)


# ── GUI Integration ─────────────────────────────────────────────

@mcp.tool()
def viora_ui_get_current_tab() -> dict:
    """Get the currently active editor tab from the running VioraEDA GUI."""
    return core.get_current_tab()


@mcp.tool()
def viora_ui_open_schematic(path: str, convert: bool = False) -> dict:
    """Open a schematic or netlist file in the running VioraEDA GUI editor."""
    return core.open_schematic(path=path, convert=convert)


@mcp.tool()
def viora_ui_open_project(path: str) -> dict:
    """Open a project file or directory in the running VioraEDA GUI."""
    return core.open_project(path)


@mcp.tool()
def viora_ui_load_simulation_results(path: str) -> dict:
    """Load a .raw simulation results file into the running VioraEDA GUI.
    This will automatically display the results in the bottom-panel Analog Oscilloscope.
    """
    return core.load_simulation_results(path)


# ── Screenshot Tools ─────────────────────────────────────────────

@mcp.tool()
def viora_screenshot_list(include_hidden: bool = False) -> dict:
    """List all visible top-level windows in the running VioraEDA GUI."""
    return core.screenshot_list(include_hidden=include_hidden)


@mcp.tool()
def viora_screenshot_capture(
    name: str,
    output: str = "",
    scale: float = 1.0,
    format: str = "PNG",
    clipboard: bool = False,
    region: str = "",
) -> Image:
    """Capture a screenshot of a window or widget by name.
    Returns a PNG image. Use name='Analog Oscilloscope' for waveforms,
    name='SchematicEditor' for the full editor, etc.
    """
    import tempfile
    if not output:
        output = tempfile.mktemp(suffix=".png")
    result = core.screenshot_capture(
        name=name, output=output, scale=scale,
        format=format, clipboard=clipboard, region=region,
    )
    if not result.get("ok"):
        raise RuntimeError(result.get("error", "Screenshot failed"))
    return Image(path=output)


# ── GUI Remote Control Tools ─────────────────────────────────────

@mcp.tool()
def viora_gui_list_elements(
    window: str = "SchematicEditor",
    filter_type: str = "",
    filter_parent: str = "",
) -> dict:
    """List interactive elements (buttons, menu actions, text fields) in a window.
    Use filter_type='QToolButton' or 'QAction' to narrow results.
    Use filter_parent='MainToolbar' to find toolbar buttons.
    """
    return core.gui_list_elements(
        window=window, filter_type=filter_type, filter_parent=filter_parent,
    )


@mcp.tool()
def viora_gui_click(target: str, window: str = "SchematicEditor") -> dict:
    """Click a button or trigger an action by label or objectName.
    Fuzzy matching: 'Run Simulation' matches 'Run Simulation (F8)'.
    """
    return core.gui_click(target=target, window=window)


@mcp.tool()
def viora_gui_type(
    field: str, text: str,
    window: str = "SchematicEditor",
    append: bool = False,
) -> dict:
    """Type text into a QLineEdit or QTextEdit field by objectName."""
    return core.gui_type(field=field, text=text, window=window, append=append)


@mcp.tool()
def viora_gui_menu(action: str, window: str = "SchematicEditor") -> dict:
    """Trigger a menu action by text label (e.g., 'Export as PDF')."""
    return core.gui_menu(action=action, window=window)


@mcp.tool()
def viora_gui_key(shortcut: str, window: str = "SchematicEditor") -> dict:
    """Send a keyboard shortcut (e.g., Ctrl+S, F8, Escape, Ctrl+Shift+K)."""
    return core.gui_key(shortcut=shortcut, window=window)


@mcp.tool()
def viora_gui_tab(tab_name: str, window: str = "SchematicEditor") -> dict:
    """Switch to a tab by name (e.g., 'xspice', 'mos_test')."""
    return core.gui_tab(tab_name=tab_name, window=window)


@mcp.tool()
def viora_gui_wait(ms: int) -> dict:
    """Wait for a specified number of milliseconds."""
    return core.gui_wait(ms=ms)


if __name__ == "__main__":
    logging.info("VioraEDA MCP server starting (stdio transport)")
    mcp.run(transport="stdio")
