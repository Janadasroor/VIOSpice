# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

"""
Model Context Protocol (MCP) server integration for VioSpice.
This module provides the core tool definitions and execution logic.
"""

import base64
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional

# ... (rest of imports)

def read_image_base64(path_str: str) -> Optional[str]:
    """Read an image file and return it as a base64 encoded string."""
    path = Path(path_str)
    if not path.exists():
        return None
    try:
        return base64.b64encode(path.read_bytes()).decode("utf-8")
    except Exception:
        return None

def pcb_render(file: str, out: str, timeout: int = 60) -> Dict[str, Any]:
    """Render a Viora .pcb board to a PNG image."""
    return run_viora_command(["render", file, out], timeout=timeout)

def schematic_render(file: str, out: str, transparent: bool = False, scale: float = 4.0, timeout: int = 60) -> Dict[str, Any]:
    """Render a Viora .flxsch schematic to a PNG image.
    If given a .cir netlist, it is auto-converted to .flxsch first.
    """
    file_path = _resolve_path(file)
    if str(file_path).lower().endswith(".cir"):
        flxsch = file_path.with_suffix(".flxsch")
        conv = netlist_to_schematic(str(file_path), out=str(flxsch))
        if not conv.get("ok"):
            return conv
        file = str(flxsch)
    else:
        file = str(file_path)
    args = ["schematic-render", file, out]
    if transparent: args.append("--transparent")
    if scale: args.extend(["--scale", str(scale)])
    return run_viora_command(args, timeout=timeout)

def symbol_render(file: str, out: str, transparent: bool = False, scale: float = 4.0, timeout: int = 60) -> Dict[str, Any]:
    """Render a Viora .viosym symbol to a PNG image."""
    args = ["symbol-render", file, out]
    if transparent: args.append("--transparent")
    if scale: args.extend(["--scale", str(scale)])
    return run_viora_command(args, timeout=timeout)

def launch_viewer(file: str, type: str = "plot") -> Dict[str, Any]:
    """Launch the standalone Waveform Viewer or Oscilloscope for a .raw file (non-blocking)."""
    if sys.platform == "linux" and not os.environ.get("DISPLAY"):
        return {"ok": False, "error": "DISPLAY is not set — cannot open GUI window on this system"}
    exe = get_viora_executable()
    try:
        args = [exe, "view", str(_resolve_path(file)), "--type", type]
        proc = subprocess.Popen(
            args,
            cwd=ROOT,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        import time
        time.sleep(1)
        if proc.poll() is not None:
            return {"ok": False, "error": f"Viewer exited immediately (code {proc.returncode}). Check DISPLAY and that '{exe}' supports the view command."}
        return {"ok": True, "message": f"Launched {type} viewer for {file}", "pid": proc.pid}
    except Exception as e:
        return {"ok": False, "error": str(e)}

def verilog_inspect(file: str, module: Optional[str] = None) -> Dict[str, Any]:
    """Inspect a SystemVerilog file to extract module and port information."""
    args = ["verilog-inspect", file, "--json"]
    if module:
        args.extend(["--module", module])
    return run_viora_command(args, json_out=True)


# Constants
ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / ".viospice"
LOG_FILE = LOG_DIR / "mcp-ml-api.log"

def get_viora_executable() -> str:
    """Locate the viora CLI binary across Linux, Windows, and Mac."""
    is_windows = sys.platform == "win32"
    ext = ".exe" if is_windows else ""
    
    candidates = [
        ROOT / "build" / f"viora{ext}",
        ROOT / "build-debug" / f"viora{ext}",
        ROOT / "build-asan" / f"viora{ext}",
        ROOT / "build-release" / f"viora{ext}",
        ROOT / "build" / f"vio-cmd{ext}",
        ROOT / "build-debug" / f"vio-cmd{ext}",
    ]
    for item in candidates:
        if item.exists():
            return str(item)
            
    # Fallback to system path
    return f"viora{ext}"

def run_viora_command(args: List[str], timeout: int = 120, json_out: bool = False, retries: int = 2) -> Dict[str, Any]:
    """Execute a viora CLI command with automatic retries for transient failures."""
    last_err = None
    for attempt in range(retries + 1):
        try:
            proc = subprocess.run(
                [get_viora_executable()] + args,
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
            
            out = proc.stdout.strip()
            err = proc.stderr.strip()
            data = None
            
            if json_out and out:
                # Robustly find the JSON part in the output (might be preceded by logs)
                json_start = out.find('{')
                json_end = out.rfind('}')
                if json_start >= 0 and json_end > json_start:
                    try:
                        data = json.loads(out[json_start : json_end + 1])
                    except json.JSONDecodeError:
                        pass
                else:
                    try:
                        data = json.loads(out)
                    except json.JSONDecodeError:
                        pass
            
            # Success
            if proc.returncode == 0:
                return {
                    "ok": True,
                    "code": proc.returncode,
                    "stdout": out,
                    "stderr": err,
                    "data": data,
                }
            
            # Failure - include context
            error_msg = f"Command failed (code {proc.returncode})"
            if err:
                error_msg += f": {err}"
            elif out and not json_out:
                error_msg += f": {out}"
                
            return {
                "ok": False,
                "error": error_msg,
                "stdout": out,
                "stderr": err,
                "code": proc.returncode,
                "data": data,
                "args": args
            }
            
        except subprocess.TimeoutExpired:
            last_err = f"Command timed out after {timeout}s"
        except Exception as e:
            last_err = str(e)
            
        if attempt < retries:
            time.sleep(0.5 * (attempt + 1))
            
    return {"ok": False, "error": last_err, "args": args}


def list_files(path_str: str = ".") -> Dict[str, Any]:
    """List files in the project directory with safety checks."""
    path = (ROOT / path_str).resolve()
    if not str(path).startswith(str(ROOT.resolve())):
        return {"ok": False, "error": "Access denied: Path is outside project root"}
    
    try:
        files = []
        for item in path.iterdir():
            files.append({
                "name": item.name,
                "type": "directory" if item.is_dir() else "file",
                "size": item.stat().st_size if item.is_file() else 0,
                "modified": item.stat().st_mtime
            })
        return {"ok": True, "path": str(path.relative_to(ROOT)), "files": files}
    except Exception as e:
        return {"ok": False, "error": str(e)}

def read_file(path_str: str) -> Dict[str, Any]:
    """Read a file from the project directory with safety checks."""
    path = (ROOT / path_str).resolve()
    if not str(path).startswith(str(ROOT.resolve())):
        return {"ok": False, "error": "Access denied: Path is outside project root"}
    
    try:
        return {"ok": True, "path": path_str, "content": path.read_text(encoding="utf-8")}
    except Exception as e:
        return {"ok": False, "error": str(e)}

def write_file(path_str: str, content: str) -> Dict[str, Any]:
    """Write a file to the project directory with safety checks."""
    path = (ROOT / path_str).resolve()
    if not str(path).startswith(str(ROOT.resolve())):
        return {"ok": False, "error": "Access denied: Path is outside project root"}
    
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return {"ok": True, "path": path_str}
    except Exception as e:
        return {"ok": False, "error": str(e)}

def raw_info(file: str) -> Dict[str, Any]:
    """Get metadata and signal list from a .raw simulation file."""
    return run_viora_command(["raw-info", file, "--json"], json_out=True)

def raw_export(file: str, out: str, fmt: str = "json") -> Dict[str, Any]:
    """Export waveform data to a different format (csv, json, parquet).

    The CLI writes the output to 'out'.  For JSON and CSV formats
    the data also appears on stdout.  We always capture stdout and
    write it to disk to be sure the file is created.
    """
    out_path = Path(out)
    out_path = out_path.expanduser().resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    result = run_viora_command(
        ["raw-export", file, "--format", fmt, "--json", "--out", str(out_path)],
        json_out=True,
    )

    # Always write stdout to the output file (CLI may not create it for
    # non-parquet formats).
    if result.get("ok") and result.get("stdout"):
        out_path.write_text(result["stdout"], encoding="utf-8")

    # Confirm the file exists on disk
    result["file_written"] = str(out_path) if out_path.exists() else ""

    return result

def symbol_list(path: str = ".") -> Dict[str, Any]:
    """List available symbols in a directory or library file."""
    return run_viora_command(["symbol-list", path, "--json"], json_out=True)

def schematic_query(file: str) -> Dict[str, Any]:
    """Get component and net information from a schematic file."""
    return run_viora_command(["schematic-query", file, "--json"], json_out=True)

def flux_run(file: str = None, code: str = None, t: float = None, inputs: list = None) -> Dict[str, Any]:
    """Run a FluxScript from a file or a string."""
    if file:
        args = ["flux", "run", file]
    elif code:
        # Use 'eval' for raw code to get immediate result
        args = ["flux", "eval", code, "--json"]
        if t is not None:
            args.extend(["--time", str(t)])
        if inputs:
            args.extend(["--inputs", ",".join(map(str, inputs))])
    else:
        return {"ok": False, "error": "Missing file or code for flux_run"}
    
    return run_viora_command(args, json_out=True)



def ui_query(code: str, host: str = "localhost", port: int = 18790) -> Dict[str, Any]:
    """Execute Python code in the running GUI context via WebSocket."""
    try:
        from vspice import ui
        proxy = ui.connect(host=host, port=port)
        result = proxy.run_python_code(code)
        return {"ok": True, "output": result.get("output", ""), "is_error": not result.get("ok", False)}
    except Exception as e:
        return {"ok": False, "error": f"VioSpice GUI not detected on {host}:{port} ({e}). Ensure the application is running with the UI Command Server enabled."}

def get_project_root() -> Dict[str, Any]:
    """Return the absolute project root path for path reference."""
    return {"ok": True, "project_root": str(ROOT.resolve())}

def get_detailed_status() -> Dict[str, Any]:
    """Get a production-grade status report of the VioSpice/Viora ecosystem."""
    exe_str = get_viora_executable()
    exe = Path(exe_str)
    status = {
        "ok": True,
        "root": str(ROOT),
        "viora_executable": str(exe),
        "viora_installed": exe.exists() and os.access(exe, os.X_OK),
        "python_version": sys.version.split()[0],
    }
    
    if status["viora_installed"]:
        try:
            res = subprocess.run([str(exe), "--version"], capture_output=True, text=True, timeout=2)
            status["viora_version"] = res.stdout.strip() or res.stderr.strip() or "v0.1.0-beta"
        except Exception:
            status["viora_version"] = "unknown"
            
    return status

def _resolve_path(path: str) -> Path:
    """Resolve a path relative to the project root if it is not absolute."""
    p = Path(path)
    if p.is_absolute():
        return p
    return (ROOT / p).resolve()

def netlist_validate(file: Optional[str] = None, cir: Optional[str] = None) -> Dict[str, Any]:
    """Validate a SPICE netlist without running simulation.

    For .flxsch files, the embedded SPICE directives are extracted
    and validated.  For .cir files and raw netlist strings, the
    content is validated directly.
    """
    if file and file.lower().endswith(".flxsch"):
        # .flxsch is JSON — extract the Spice Directive text and validate that
        try:
            sch_path = _resolve_path(file)
            sch = json.loads(sch_path.read_text(encoding="utf-8"))
        except Exception as e:
            return {"ok": False, "error": f"Cannot read .flxsch: {e}"}

        directives = []
        for item in sch.get("items", []):
            if item.get("type") == "Spice Directive" and item.get("text"):
                directives.append(item["text"])

        if not directives:
            return {"ok": False, "error": "No SPICE directives found in .flxsch file."}

        cir_content = "\n".join(directives)
        return netlist_validate(cir=cir_content)

    args = ["netlist-validate"]
    if file:
        args.append(str(_resolve_path(file)))
        return run_viora_command(args, json_out=True)
    elif cir:
        with tempfile.NamedTemporaryFile(mode='w', suffix='.cir', delete=False) as tf:
            tf.write(cir)
            temp_name = tf.name
        try:
            args.append(temp_name)
            return run_viora_command(args, json_out=True)
        finally:
            if os.path.exists(temp_name): os.remove(temp_name)
    return {"ok": False, "error": "No file or netlist content provided"}

def netlist_run(
    file: Optional[str] = None,
    cir: Optional[str] = None,
    analysis: Optional[str] = None,
    stop: Optional[str] = None,
    step: Optional[str] = None,
    signals: Optional[List[str]] = None,
    json_out: bool = True,
    robust: bool = False,
    compat: bool = True,
    smart_signals: Optional[List[Dict[str, Any]]] = None,
    verilog_blocks: Optional[List[Dict[str, Any]]] = None,
    xspice_blocks: Optional[List[Dict[str, Any]]] = None,
    options: Optional[str] = None,
    temperature: Optional[float] = None,
) -> Dict[str, Any]:
    """Execute a SPICE or VioSpice simulation."""
    v_args = ["netlist-run"]
    target = file
    temp_file = None
    temp_sv_files = []

    # Handle Smart Signals, Verilog Blocks, and XSPICE Blocks by wrapping in a .flxsch
    if smart_signals or verilog_blocks or xspice_blocks:
        items = []
        hybrid_netlist = cir or ""
        if not hybrid_netlist and file:
            try:
                fpath = _resolve_path(file)
                ext = fpath.suffix.lower()
                if ext in (".cir", ".net", ".sp"):
                    hybrid_netlist = fpath.read_text(encoding="utf-8")
                elif ext == ".flxsch":
                    orig = json.loads(fpath.read_text(encoding="utf-8"))
                    items = orig.get("items", [])
            except Exception:
                pass
        
        # 1. FluxScript blocks (Smart Signals)
        if smart_signals:
            for ss in smart_signals:
                ref = ss["ref"]
                items.append({
                    "type": "SmartSignalBlock",
                    "reference": ref,
                    "fluxCode": ss["code"],
                    "engineType": "flux",
                    "inputs": ss.get("inputs", []),
                    "outputs": ss.get("outputs", []),
                    "excludeFromSim": True
                })
                # Binding logic
                if "inputs" in ss and "outputs" in ss:
                    in_nets = " ".join(ss["inputs"]) if ss["inputs"] else "0"
                    out_nets = " ".join(ss["outputs"])
                    hybrid_netlist += f"\n* Smart Signal Block: {ref}\n"
                    hybrid_netlist += f"A_{ref} [{in_nets}] {out_nets} viospice_jit_model_{ref}\n"
                    hybrid_netlist += f".model viospice_jit_model_{ref} viospice_jit (jit_id=\"{ref}\")\n"
                else:
                    hybrid_netlist += f"\n* Smart Signal Model: {ref}\n"
                    hybrid_netlist += f".model viospice_jit_model_{ref} viospice_jit (jit_id=\"{ref}\")\n"

        # 2. Verilog Blocks
        if verilog_blocks:
            # ... (unchanged)
            for vb in verilog_blocks:
                ref = vb["ref"]
                sv_path = vb.get("file")
                if not sv_path and "code" in vb:
                    # Save code to temp .sv file
                    fd, sv_temp = tempfile.mkstemp(suffix=".sv", dir=ROOT)
                    os.close(fd)
                    Path(sv_temp).write_text(vb["code"], encoding="utf-8")
                    sv_path = sv_temp
                    temp_sv_files.append(sv_temp)
                
                module_name = vb.get("module", "top")
                
                # 2a. Register as Code Carrier for the JIT compiler
                items.append({
                    "type": "SystemVerilogBlock",
                    "reference": ref,
                    "value": sv_path,
                    "svFilePath": sv_path,
                    "moduleName": module_name,
                    "systemVerilogModule": module_name,
                    "excludeFromSim": True
                })
                
                # 2b. Manually bind to netlist via A-devices (supports multi-bit)
                inspect_res = verilog_inspect(sv_path, module=module_name)
                ports = []
                if inspect_res.get("ok") and "data" in inspect_res:
                    ports = inspect_res["data"].get("ports", [])
                
                if ports:
                    in_ports = [(p["name"], p.get("width", 1)) for p in ports if p["direction"] == "input"]
                    out_ports = [(p["name"], p.get("width", 1)) for p in ports if p["direction"] == "output"]
                    
                    user_inputs = vb.get("inputs", [])
                    user_outputs = vb.get("outputs", [])
                    
                    # Expand multi-bit inputs into per-bit nets, LSB-first for each port
                    in_nets = []
                    ui_idx = 0
                    for pname, pwidth in in_ports:
                        for b in range(pwidth):
                            net = user_inputs[ui_idx] if ui_idx < len(user_inputs) else "0"
                            in_nets.append(net)
                            ui_idx += 1
                    in_vector = "[" + " ".join(in_nets) + "]" if in_nets else "0"
                    
                    # Generate one A-device per output bit
                    hybrid_netlist += f"\n* Verilog Block: {ref} ({module_name})\n"
                    uo_idx = 0
                    for pname, pwidth in out_ports:
                        for b in range(pwidth):
                            out_net = user_outputs[uo_idx] if uo_idx < len(user_outputs) else "0"
                            # Use bare pin name for 1-bit ports (matching compileFluxScripts registration),
                            # or _b suffix for multi-bit ports
                            pin_key = pname.upper() if pwidth == 1 else f"{pname.upper()}_{b}"
                            jit_id = f"{ref}_{pin_key}"
                            hybrid_netlist += f"A_{jit_id} {in_vector} {out_net} viospice_jit_model_{jit_id}\n"
                            hybrid_netlist += f".model viospice_jit_model_{jit_id} viospice_jit (jit_id=\"{jit_id}\")\n"
                            uo_idx += 1

        # 3. XSPICE Blocks
        if xspice_blocks:
            for xb in xspice_blocks:
                ref = xb["ref"]
                model_type = xb["model"]
                inputs = xb.get("inputs", [])
                outputs = xb.get("outputs", [])
                params = xb.get("params", {})
                
                # Heuristics for vector ports (brackets)
                is_logic = model_type.startswith("d_")
                is_bridge = "bridge" in model_type.lower()
                
                # Default logic gates: inputs are vector, outputs are scalar
                # EXCEPT bridges: both are vector
                # EXCEPT inverter/buffer: both are scalar
                default_in_vec = False
                default_out_vec = False
                
                if is_bridge:
                    default_in_vec = True
                    default_out_vec = True
                elif is_logic:
                    if model_type in ["d_inverter", "d_buffer", "d_tristate"]:
                        default_in_vec = False
                        default_out_vec = False
                    else:
                        default_in_vec = True
                        default_out_vec = False
                
                in_vec = xb.get("input_vector", default_in_vec)
                out_vec = xb.get("output_vector", default_out_vec)
                
                # Metadata for GUI
                # ... (rest of logic unchanged)
                items.append({
                    "type": "XspiceBlock",
                    "reference": ref,
                    "xspice_modelType": model_type,
                    "xspice_params": json.dumps(params),
                    "excludeFromSim": True
                })
                
                # Netlist emission
                model_name = f"m_{ref}_{model_type}"
                param_str = " ".join([f"{k}={v}" for k, v in params.items()])
                
                in_str = "[" + " ".join(inputs) + "]" if in_vec else " ".join(inputs)
                if not inputs and not in_vec: in_str = "0"
                elif not inputs and in_vec: in_str = "[0]"
                
                out_str = "[" + " ".join(outputs) + "]" if out_vec else " ".join(outputs)
                if not outputs and not out_vec: out_str = "0"
                elif not outputs and out_vec: out_str = "[0]"

                hybrid_netlist += f"\n* XSPICE Block: {ref} ({model_type})\n"
                hybrid_netlist += f"A_{ref} {in_str} {out_str} {model_name}\n"
                hybrid_netlist += f".model {model_name} {model_type}({param_str})\n"

        # 4. Combined netlist
        hybrid_netlist += "\n.save all\n"
        if analysis:
            # Build analysis line with optional step/stop
            if not analysis.startswith("."):
                analysis_line = f".{analysis}"
            else:
                analysis_line = analysis
            if analysis.lower().replace(".", "") == "tran":
                if step:
                    analysis_line += f" {step}"
                if stop:
                    analysis_line += f" {stop}"
            hybrid_netlist += analysis_line + "\n"
        
        items.append({
            "type": "Spice Directive",
            "text": hybrid_netlist
        })
        
        # 3. Create .flxsch JSON
        sch_json = {
            "metadata": {"application": "viospice", "version": 1},
            "items": items
        }
        
        fd, temp_file = tempfile.mkstemp(suffix=".flxsch", dir=ROOT)
        os.close(fd)
        Path(temp_file).write_text(json.dumps(sch_json), encoding="utf-8")
        target = temp_file
    elif not target and cir:
        # Standard .cir transient file
        fd, temp_file = tempfile.mkstemp(suffix=".cir", dir=ROOT)
        os.close(fd)
        Path(temp_file).write_text(cir, encoding="utf-8")
        target = temp_file
            
    # Inject SPICE options and temperature as netlist directives
    # (the CLI doesn't have --options/--temp flags yet)
    extra_directives = []
    if options:
        extra_directives.append(f".options {options}")
    if temperature is not None:
        extra_directives.append(f".temp {temperature}")

    if extra_directives and temp_file is None and cir:
        # Create a temp file so we can inject directives
        fd, temp_file = tempfile.mkstemp(suffix=".cir", dir=ROOT)
        os.close(fd)
        Path(temp_file).write_text(cir + "\n" + "\n".join(extra_directives), encoding="utf-8")
        target = temp_file
    elif extra_directives and file and file.lower().endswith(".flxsch"):
        # Inject directives into the .flxsch by adding a Spice Directive item
        try:
            sch_path = _resolve_path(file)
            sch = json.loads(sch_path.read_text(encoding="utf-8"))
            sch.setdefault("items", []).append({
                "type": "Spice Directive",
                "text": "\n".join(extra_directives),
            })
            fd, temp_file = tempfile.mkstemp(suffix=".flxsch", dir=ROOT)
            os.close(fd)
            Path(temp_file).write_text(json.dumps(sch), encoding="utf-8")
            target = temp_file
        except Exception as e:
            return {"ok": False, "error": f"Cannot inject options into .flxsch: {e}"}

    if target: v_args.append(target)
    if analysis: v_args.extend(["--analysis", analysis])
    if stop: v_args.extend(["--stop", stop])
    if step: v_args.extend(["--step", step])
    if signals: 
        for s in signals:
            v_args.extend(["--signal", s])
    if robust: v_args.append("--robust")
    if compat: v_args.append("--compat")
    if json_out: v_args.append("--json")
    
    try:
        res = run_viora_command(v_args, json_out=json_out)
        if res.get("ok") and res.get("data"):
            # Flatten data into top-level for ergonomics
            data = res.pop("data")
            res.update(data)
            
            # Alias rawPath to raw_path for Pythonic consistency
            if "rawPath" in res:
                res["raw_path"] = res["rawPath"]
        return res
    finally:
        if temp_file and os.path.exists(temp_file):
            os.remove(temp_file)
        for svf in temp_sv_files:
            if os.path.exists(svf):
                os.remove(svf)


def netlist_to_schematic(file: str, out: Optional[str] = None) -> Dict[str, Any]:
    """Convert a SPICE netlist (.cir) into a Viora schematic (.flxsch).
    AI agents should use this after verifying a circuit works via netlist_run.
    """
    args = ["netlist-to-schematic", str(_resolve_path(file))]
    if out:
        args.extend(["--out", out])
    
    return run_viora_command(args, json_out=True)


def open_schematic(path: str, convert: bool = False) -> Dict[str, Any]:
    """Open a schematic or netlist file in the running VioSpice GUI."""
    abs_path = os.path.abspath(path)
    
    try:
        from vspice import ui
        proxy = ui.connect()
        
        if convert and (path.endswith(".cir") or path.endswith(".net")):
            # Construction of temporary schematic with Spice Directive
            # needs to stay here for AI-to-Visual bridge
            path_obj = Path(abs_path)
            # Try high-level conversion via Python snippet in GUI
            # actually let's try to keep it simple and just open if convert is false
            # but user might want conversion.
            # For now, let's prioritize simple opening of the .flxsch I created.
            pass

        result = proxy.open_schematic(abs_path)
        return {"ok": result.get("ok", False), "error": result.get("error", "")}
    except Exception as e:
        return {"ok": False, "error": f"VioSpice GUI not detected on localhost:18790 ({e})."}


def open_project(path: str) -> Dict[str, Any]:
    """Open a project file or directory in the running VioSpice GUI."""
    abs_path = os.path.abspath(path)
    try:
        from vspice import ui
        proxy = ui.connect()
        result = proxy.open_project(abs_path)
        return {"ok": result.get("ok", False), "error": result.get("error", "")}
    except Exception as e:
        return {"ok": False, "error": f"VioSpice GUI not detected on localhost:18790 ({e})."}


def load_simulation_results(path: str) -> Dict[str, Any]:
    """Load a .raw simulation results file into the running VioSpice GUI (Analog Oscilloscope)."""
    abs_path = os.path.abspath(path)
    try:
        from vspice import ui
        proxy = ui.connect()
        result = proxy.load_simulation_results(abs_path)
        return {"ok": result.get("ok", False), "error": result.get("error", "")}
    except Exception as e:
        return {"ok": False, "error": f"VioSpice GUI not detected on localhost:18790 ({e})."}


def get_current_tab() -> Dict[str, Any]:

    """Query the running GUI for the currently active tab."""
    # This snippet looks for the first visible QTabWidget and gets its tab text
    code = """
import vspice
from PySide6.QtWidgets import QApplication, QTabWidget

def _find_active_tab():
    for w in QApplication.topLevelWidgets():
        if not w.isVisible(): continue
        tabs = w.findChild(QTabWidget)
        if tabs and tabs.isVisible():
            return tabs.tabText(tabs.currentIndex())
    return "Project Manager" # Default if no tabs found (usually PM window)

print(_find_active_tab())
"""
    return ui_query(code)


# ── Async simulation jobs ───────────────────────────────────────

_jobs: Dict[str, Dict[str, Any]] = {}
_job_lock = threading.Lock()
_next_job_id = 0

def netlist_run_async(
    file: Optional[str] = None,
    cir: Optional[str] = None,
    analysis: Optional[str] = None,
    stop: Optional[str] = None,
    step: Optional[str] = None,
    signals: Optional[List[str]] = None,
    robust: bool = False,
    compat: bool = True,
    smart_signals: Optional[List[Dict[str, Any]]] = None,
    verilog_blocks: Optional[List[Dict[str, Any]]] = None,
    xspice_blocks: Optional[List[Dict[str, Any]]] = None,
    options: Optional[str] = None,
    temperature: Optional[float] = None,
) -> Dict[str, Any]:
    """Start a simulation in the background and return a job ID immediately.
    
    Poll progress with `netlist_job_status(job_id)` and retrieve
    results with `netlist_job_result(job_id)` once status is 'done'.
    """
    global _next_job_id
    with _job_lock:
        job_id = f"sim_{_next_job_id}"
        _next_job_id += 1
        _jobs[job_id] = {"status": "queued", "progress": 0, "result": None}

    def _run():
        with _job_lock:
            _jobs[job_id]["status"] = "running"
            _jobs[job_id]["progress"] = 10
        try:
            result = netlist_run(
                file=file, cir=cir,
                analysis=analysis, stop=stop, step=step,
                signals=signals, json_out=True,
                robust=robust, compat=compat,
                smart_signals=smart_signals,
                verilog_blocks=verilog_blocks,
                xspice_blocks=xspice_blocks,
                options=options, temperature=temperature,
            )
            with _job_lock:
                _jobs[job_id]["status"] = "done"
                _jobs[job_id]["progress"] = 100
                _jobs[job_id]["result"] = result
        except Exception as e:
            with _job_lock:
                _jobs[job_id]["status"] = "error"
                _jobs[job_id]["result"] = {"ok": False, "error": str(e)}

    thread = threading.Thread(target=_run, daemon=True)
    thread.start()
    return {"ok": True, "job_id": job_id, "status": "queued"}


def netlist_job_status(job_id: str) -> Dict[str, Any]:
    """Check the progress of an async simulation job."""
    with _job_lock:
        job = _jobs.get(job_id)
        if not job:
            return {"ok": False, "error": f"Unknown job: {job_id}"}
        return {
            "ok": True,
            "job_id": job_id,
            "status": job["status"],
            "progress": job["progress"],
        }


def netlist_job_result(job_id: str) -> Dict[str, Any]:
    """Retrieve the result of a completed async simulation job."""
    with _job_lock:
        job = _jobs.get(job_id)
        if not job:
            return {"ok": False, "error": f"Unknown job: {job_id}"}
        if job["status"] not in ("done", "error"):
            return {"ok": False, "error": f"Job {job_id} is still {job['status']} (progress {job['progress']}%)"}
        result = job["result"]
        # Clean up completed jobs
        del _jobs[job_id]
        return {"ok": True, "job_id": job_id, "result": result}


# ── Screenshot Tools ─────────────────────────────────────────────

def screenshot_list(include_hidden: bool = False) -> Dict[str, Any]:
    """List all visible top-level windows."""
    args = ["screenshot", "--json"]
    if include_hidden:
        args.append("--include-hidden")
    return run_viora_command(args, json_out=True)


def screenshot_capture(
    name: str,
    output: str = "",
    scale: float = 1.0,
    format: str = "PNG",
    clipboard: bool = False,
    region: str = "",
) -> Dict[str, Any]:
    """Capture a screenshot of a window or widget by name."""
    args = ["screenshot", "--name", name, "--format", format, "--scale", str(scale), "--json"]
    if output:
        args.extend(["--output", output])
    if clipboard:
        args.append("--clipboard")
    if region:
        args.extend(["--region", region])
    return run_viora_command(args, json_out=True)


# ── GUI Remote Control Tools ─────────────────────────────────────

def gui_list_elements(
    window: str = "SchematicEditor",
    filter_type: str = "",
    filter_parent: str = "",
) -> Dict[str, Any]:
    """List interactive elements (buttons, menu actions, text fields) in a window."""
    args = ["gui", "list-buttons", "--window", window, "--json"]
    if filter_type:
        args.extend(["--type", filter_type])
    if filter_parent:
        args.extend(["--parent", filter_parent])
    return run_viora_command(args, json_out=True)


def gui_click(target: str, window: str = "SchematicEditor") -> Dict[str, Any]:
    """Click a button or trigger an action by label or objectName."""
    args = ["gui", "click", target, "--window", window, "--json"]
    return run_viora_command(args, json_out=True)


def gui_type(
    field: str, text: str,
    window: str = "SchematicEditor",
    append: bool = False,
) -> Dict[str, Any]:
    """Type text into a QLineEdit or QTextEdit field."""
    args = ["gui", "type", field, text, "--window", window, "--json"]
    if append:
        args.append("--append")
    return run_viora_command(args, json_out=True)


def gui_menu(action: str, window: str = "SchematicEditor") -> Dict[str, Any]:
    """Trigger a menu action by text label."""
    args = ["gui", "menu", action, "--window", window, "--json"]
    return run_viora_command(args, json_out=True)


def gui_key(shortcut: str, window: str = "SchematicEditor") -> Dict[str, Any]:
    """Send a keyboard shortcut (e.g., Ctrl+S, F8, Escape)."""
    args = ["gui", "key", shortcut, "--window", window, "--json"]
    return run_viora_command(args, json_out=True)


def gui_tab(tab_name: str, window: str = "SchematicEditor") -> Dict[str, Any]:
    """Switch to a tab by name."""
    args = ["gui", "tab", tab_name, "--window", window, "--json"]
    return run_viora_command(args, json_out=True)


def gui_wait(ms: int) -> Dict[str, Any]:
    """Wait for a specified number of milliseconds."""
    args = ["gui", "wait", str(ms)]
    return run_viora_command(args, json_out=True)

