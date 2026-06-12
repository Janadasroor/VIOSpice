# VioSpice AI Engineering Mandates

This file contains absolute instructions for AI agents working on the VioSpice/Viora ecosystem.

## Circuit Design Workflow (MANDATORY)

To prevent errors in complex JSON schematic generation, AI agents MUST follow the **Netlist-First** workflow:

1.  **SPICE First**: When asked to create a circuit, first write a standard SPICE netlist (`.cir`) using the `viora_write_file` tool.
2.  **Verify**: Run the simulation using `viospice_netlist_run` (passing the `.cir` file path) and verify the outputs using `viospice_raw_export`.
3.  **Catch Results in GUI**: After a successful simulation, use `viora_ui_load_simulation_results(path="...")` to automatically display the waveforms in the VioraEDA **Simulation Panel (Bottom Dock)**.
4.  **Launch Standalone Scope (MANDATORY)**: Immediately after a successful simulation, ALWAYS use `viospice_launch_viewer(file="...", type="osc")` to launch the **standalone Analog Oscilloscope** window. This provides high-fidelity visual feedback and is the preferred interactive workflow.
5.  **Convert**: Once the circuit is verified to be working correctly, use the `viospice_netlist_to_schematic` tool to convert the working `.cir` into a visual `.flxsch` file.
6.  **Visualize**: Provide the user with a visual confirmation using `viora_schematic_render`.

## Smart Signal JIT Mandates

1.  **Mandatory Return**: FluxScript code inside a Smart Signal block is automatically wrapped in a function with braces (`{ ... }`). You MUST use the `return` keyword to output a value. Without `return`, the block will output 0V.
2.  **Case Insensitivity**: JIT function lookups are now case-insensitive to handle ngspice's lowercase identifier conversion. You can use `UB1` or `ub1` interchangeably in parameters.
3.  **No Nested Defs**: Do not include `def update(...)` blocks inside the Smart Signal editor; they will be treated as nested functions and will not return values to the simulation engine.

**DO NOT attempt to write `.flxsch` JSON files manually unless specifically instructed by the user.** The automated conversion tool ensures the schematic is structurally sound and synchronized with the simulation logic.
