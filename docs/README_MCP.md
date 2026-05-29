# VioSpice & Viora Master-Level MCP Interface

This repository contains a production-grade **Model Context Protocol (MCP)** server that allows AI agents (Gemini, Claude, Cursor) to interact directly with the **VioSpice** simulation engine and the **Viora** design ecosystem.

## Engineering Branding

- **VioSpice**: Refers to the high-performance SPICE simulation engine, binary waveform processing, and solver core.
- **Viora**: Refers to the AEDA (Automated Electronic Design Automation) suite, including schematic entry, PCB layout, symbol management, and design automation.

## Master-Level Features

- **Autonomous Engineering**: The AI can browse VioSpice/Viora project files, read schematics, modify designs, and run simulations.
- **Visual Intelligence**: Tools to render **Viora** schematics and symbols to PNG for AI "vision" analysis.
- **Deep Data Access**: Export binary **VioSpice** `.raw` waveforms to JSON/CSV/Parquet for structured analysis.
- **Robust Execution**: Automatic retries and transient failure handling for high-load simulation environments.
- **Safety Isolated**: Strict path validation prevents access outside the project root.

## Available Tools

### Viora (Design & Design Automation)
- `viora_status`: System health check for design and simulation engines.
- `viora_list_files`: Project workspace exploration.
- `viora_read_file` / `viora_write_file`: Design iteration and file management.
- `viora_symbol_list`: Query the Viora component library.
- `viora_schematic_query`: Extract netlist and component data from `.flxsch` files.
- `viora_pcb_render`: Render a PCB board to a PNG image for visual inspection.
- `viora_schematic_render` / `viora_symbol_render`: Visual inspection tools for schematics and symbols.
- `viora_flux_run`: Advanced design automation via FluxScript.
- `viora_flux_help`: Get FluxScript syntax and API documentation.
- `viora_ui_get_current_tab`: Real-time VioSpice GUI state synchronization.
- `viora_ui_open_project`: Open the current project workspace in the VioraEDA GUI.
- `viora_ui_open_schematic`: Open a specific schematic or netlist in the GUI.
- `viora_ui_load_simulation_results`: Push `.raw` simulation data into the GUI's Analog Oscilloscope (bottom panel).


> **Note**: All rendering tools return the actual image data directly to the AI agent (via base64), enabling visual reasoning for design tasks.


### VioSpice (Simulation)
- `viospice_netlist_run`: High-performance SPICE/VioSpice simulation execution. Supports:
    - **Raw SPICE**: Classic `.cir` netlists.
    - **FluxScript JIT**: Algorithmic behavioral blocks using `smart_signals`.
    - **SystemVerilog**: RTL-in-SPICE simulation using `verilog_blocks`.
    - **Important**: See [`AI_VERILOG_QUICKSTART.md`](AI_VERILOG_QUICKSTART.md) for the supported SV subset and working templates.
    - **XSPICE Blocks**: Standard XSPICE models (gates, bridges, math) using `xspice_blocks`.
- `viospice_netlist_to_schematic`: Convert a proven `.cir` netlist into a visual `.flxsch` schematic.
- `viospice_raw_info`: Extract signal metadata from binary simulation results.
- `viospice_raw_export`: Convert binary waveforms to structured data formats.

#### Preferred Workflow: Netlist-First Design
To ensure high-quality results, AI agents should follow this sequence:
1.  **Write and Test**: Write a raw SPICE netlist (`.cir`) and verify it using `viospice_netlist_run`.
2.  **Iterate**: Fix any simulation errors or logic bugs in the `.cir` file.
3.  **Schematic Generation**: Once the circuit is working, call `viospice_netlist_to_schematic` to generate the visual `.flxsch` file for the user.
4.  **Visualize**: Use `viora_schematic_render` to provide a visual preview of the final design.

#### Advanced: Hybrid Design with `viospice_netlist_run`
You can mix analog SPICE with behavioral blocks by providing the following arguments:

```json
{
  "analysis": "tran 1u 5m",
  "cir": "V1 IN 0 SINE(0 5 1k)\nRLOAD OUT 0 1k",
  "smart_signals": [
    {
      "ref": "SB1",
      "code": "def update(t, inputs) { return inputs[0] * 2.0; }",
      "inputs": ["IN"],
      "outputs": ["OUT"]
    }
  ],
  "xspice_blocks": [
    {
      "ref": "U1",
      "model": "d_and",
      "inputs": ["DIG1", "DIG2"],
      "outputs": ["DIG_OUT"]
    }
  ]
}
```

## Validation Status

The engine has passed the **VioSpice/Viora Master Validation Suite**:
- **200/200 Autonomous Simulations** passed.
- **Behavioral Analysis**: Every simulation verified for physical correctness (gain, clipping levels, resonance).
- **Environment**: Verified across macOS, Linux, and Windows (bundled Python mode).

## Setup for AI IDEs

Add the following to your MCP configuration:

```json
{
  "mcpServers": {
    "viora_viospice": {
      "command": "python3",
      "args": ["/path/to/viospice/python/scripts/viospice_mcp_server.py"]
    }
  }
}
```
