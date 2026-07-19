#!/usr/bin/env python3
import os
import sys
import re
import json
import subprocess
import argparse
from multiprocessing import Pool

def get_symbols_in_file(filepath):
    symbols = []
    symbol_pat = re.compile(r'^\s*\(symbol\s+"([^"]+)"')
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                m = symbol_pat.match(line)
                if m:
                    symbols.append(m.group(1))
    except Exception as e:
        print(f"Failed to read {filepath}: {e}", file=sys.stderr)
    return symbols

def convert_single_symbol(task):
    viora_path, filepath, category, symbol_name, target_dir = task
    sanitized_symbol_name = symbol_name.replace("/", "_").replace("\\", "_")
    target_path = os.path.join(target_dir, f"{sanitized_symbol_name}.viosym")
    
    os.makedirs(target_dir, exist_ok=True)
    
    args = [
        viora_path,
        "symbol-import",
        filepath,
        target_path,
        "--symbol-name",
        symbol_name
    ]
    
    try:
        subprocess.run(args, capture_output=True, check=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"Failed to convert {symbol_name} in {filepath}: {e.stderr.decode()}", file=sys.stderr)
        return False

def main():
    parser = argparse.ArgumentParser(description="Convert modern KiCad S-expression symbol libraries to native .viosym files.")
    parser.add_argument("--viora-path", default="./build/viora", help="Path to viora CLI executable")
    parser.add_argument("--kicad-sym-dir", default="/home/jnd/electronic-projects/kicad-symbols-v6", help="Path to cloned kicad-symbols S-expression directory")
    parser.add_argument("--target-dir", default="/home/jnd/ViospiceLib/sym", help="Target dir to write converted viosyms")
    parser.add_argument("--category", help="Only convert libraries matching this category name (e.g. Connector)")
    parser.add_argument("--workers", type=int, default=3, help="Number of parallel workers (capped default to avoid machine lockup)")
    
    args = parser.parse_args()
    
    viora_path = os.path.abspath(args.viora_path)
    kicad_sym_dir = os.path.abspath(args.kicad_sym_dir)
    target_dir = os.path.abspath(args.target_dir)
    
    if not os.path.exists(viora_path):
        print(f"Error: viora executable not found at {viora_path}", file=sys.stderr)
        sys.exit(1)
        
    if not os.path.exists(kicad_sym_dir):
        print(f"Error: KiCad symbols library not found at {kicad_sym_dir}", file=sys.stderr)
        sys.exit(1)
        
    print(f"Scanning for symbol files in {kicad_sym_dir}...")
    
    symbol_files = []
    for root, dirs, files in os.walk(kicad_sym_dir):
        for f in files:
            if f.endswith(".kicad_sym"):
                filepath = os.path.join(root, f)
                
                # Determine Category name based on parent folder (.kicad_symdir name)
                parent_dir = os.path.basename(root)
                category = parent_dir
                if category.endswith(".kicad_symdir"):
                    category = category[:-13]
                    
                if args.category and args.category.lower() not in category.lower():
                    continue
                    
                symbol_files.append((filepath, category))
                
    print(f"Found {len(symbol_files)} library/symbol source files.")
    
    # 1. Discover all symbols to convert using regex S-expression parser
    tasks = []
    print("Discovering symbols in files...")
    for filepath, category in symbol_files:
        symbols = get_symbols_in_file(filepath)
        cat_target_dir = os.path.join(target_dir, category)
        for sym in symbols:
            tasks.append((viora_path, filepath, category, sym, cat_target_dir))
            
    print(f"Discovered {len(tasks)} symbols to convert.")
    
    if not tasks:
        print("No symbols discovered. Exiting.")
        return
        
    print(f"Converting using {args.workers} parallel workers...")
    
    success_count = 0
    with Pool(args.workers) as pool:
        results = pool.map(convert_single_symbol, tasks)
        success_count = sum(1 for r in results if r)
        
    print(f"Conversion complete: Successfully converted {success_count}/{len(tasks)} symbols.")

if __name__ == "__main__":
    main()
