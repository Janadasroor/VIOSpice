#!/usr/bin/env python3
"""Extract sections from pdfpcb-audit.cpp and write to target files."""

import os

SRC = "/home/jnd/Downloads/vit-onnx/pdfpcb-audit.cpp"
TGT_DIR = "/home/jnd/qt_projects/viospice"

# Each section: (relative_target_path, start_marker, end_marker_or_None)
# end_marker=None means end of file
sections = [
    ("pcb/editor/pcb_export_manager.h",
     "// ===== File: pcb/editor/pcb_export_manager.h =====",
     "// ===== File: pcb/editor/pcb_export_manager.cpp ====="),
    ("pcb/editor/pcb_export_manager.cpp",
     "// ===== File: pcb/editor/pcb_export_manager.cpp =====",
     "// ===== File: pcb/dialogs/gerber_export_dialog.h ====="),
    ("pcb/dialogs/gerber_export_dialog.h",
     "// ===== File: pcb/dialogs/gerber_export_dialog.h =====",
     "// ===== File: pcb/dialogs/gerber_export_dialog.cpp ====="),
    ("pcb/dialogs/gerber_export_dialog.cpp",
     "// ===== File: pcb/dialogs/gerber_export_dialog.cpp =====",
     "// ===== File: pcb/dialogs/pdf_viewer_dialog.h ====="),
    ("pcb/dialogs/pdf_viewer_dialog.h",
     "// ===== File: pcb/dialogs/pdf_viewer_dialog.h =====",
     "// ===== File: pcb/dialogs/pdf_viewer_dialog.cpp ====="),
    ("pcb/dialogs/pdf_viewer_dialog.cpp",
     "// ===== File: pcb/dialogs/pdf_viewer_dialog.cpp =====",
     "// ===== File: pcb/analysis/design_report_generator.h ====="),
    ("pcb/analysis/design_report_generator.h",
     "// ===== File: pcb/analysis/design_report_generator.h =====",
     "// ===== File: pcb/analysis/design_report_generator.cpp ====="),
    ("pcb/analysis/design_report_generator.cpp",
     "// ===== File: pcb/analysis/design_report_generator.cpp =====",
     None),
]

with open(SRC, "r") as f:
    lines = f.readlines()

for rel_path, start_marker, end_marker in sections:
    start_idx = None
    for i, line in enumerate(lines):
        if start_marker in line:
            start_idx = i
            break
    if start_idx is None:
        print(f"ERROR: start marker '{start_marker}' not found")
        continue

    if end_marker is None:
        end_idx = len(lines)
    else:
        end_idx = None
        for i, line in enumerate(lines):
            if end_marker in line and i > start_idx:
                end_idx = i
                break
        if end_idx is None:
            print(f"ERROR: end marker '{end_marker}' not found")
            continue

    tgt_path = os.path.join(TGT_DIR, rel_path)
    content = "".join(lines[start_idx:end_idx])
    with open(tgt_path, "w") as f:
        f.write(content)
    print(f"Wrote {len(content)} bytes ({end_idx - start_idx} lines) to {tgt_path}")
