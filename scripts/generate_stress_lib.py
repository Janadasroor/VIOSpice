# Copyright 2026 Janada Sroor
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

import os
import json
from pathlib import Path

def generate_stress_library(root_path, num_folders=10, symbols_per_folder=1000):
    """
    Generates a massive amount of .viosym files for GUI stress testing.
    """
    root = Path(root_path).expanduser()
    root.mkdir(parents=True, exist_ok=True)
    
    print(f"Generating {num_folders * symbols_per_folder} symbols in {root}...")
    
    # Template for a minimal valid symbol to keep disk usage sane
    symbol_template = {
        "name": "",
        "description": "Stress Test Fake Component",
        "category": "StressTest",
        "referencePrefix": "U",
        "primitives": [
            {
                "type": "rect",
                "x": -25, "y": -25, "w": 50, "h": 50,
                "filled": False
            },
            {
                "type": "text",
                "text": "FAKE",
                "x": -15, "y": -5,
                "fontSize": 8
            }
        ]
    }
    
    total_count = 0
    for f in range(num_folders):
        folder_name = f"Category_{f+1:03d}"
        folder_path = root / folder_name
        folder_path.mkdir(exist_ok=True)
        
        for s in range(symbols_per_folder):
            sym_name = f"FAKE_COMP_{f+1:03d}_{s+1:04d}"
            sym_data = symbol_template.copy()
            sym_data["name"] = sym_name
            
            file_path = folder_path / f"{sym_name.lower()}.viosym"
            with open(file_path, 'w') as jf:
                json.dump(sym_data, jf)
            
            total_count += 1
            if total_count % 1000 == 0:
                print(f"Progress: {total_count} symbols created...")

    print(f"Done! Successfully created {total_count} symbols.")

if __name__ == "__main__":
    # Target the user's ViospiceLib/sym folder
    target = "~/ViospiceLib/sym/StressTest"
    generate_stress_library(target, num_folders=10, symbols_per_folder=1000)
