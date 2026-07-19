#!/usr/bin/env python3
import os
import sys
import re
import json
import shutil
import subprocess
import argparse
from multiprocessing import Pool

def combine_symdir_to_file(symdir_path, output_filepath):
    sym_files = [os.path.join(symdir_path, f) for f in os.listdir(symdir_path) if f.endswith('.kicad_sym')]
    
    symbol_blocks = []
    version_line = '(version 20251024)'
    generator_line = '(generator "kicad_symbol_editor")'
    
    for sf in sym_files:
        try:
            with open(sf, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                
                depth = 0
                in_quotes = False
                escape = False
                start_idx = -1
                
                i = 0
                n = len(content)
                while i < n:
                    c = content[i]
                    if escape:
                        escape = False
                        i += 1
                        continue
                    if c == '\\':
                        escape = True
                        i += 1
                        continue
                    if c == '"':
                        in_quotes = not in_quotes
                        i += 1
                        continue
                    if not in_quotes:
                        if c == '(':
                            depth += 1
                            if depth == 2:
                                if content[i+1:i+8] == "symbol ":
                                    start_idx = i
                        elif c == ')':
                            if depth == 2 and start_idx != -1:
                                symbol_blocks.append(content[start_idx:i+1])
                                start_idx = -1
                            depth -= 1
                    i += 1
        except Exception as e:
            print(f"Failed to read {sf} for combining: {e}", file=sys.stderr)
            
    os.makedirs(os.path.dirname(output_filepath), exist_ok=True)
    with open(output_filepath, 'w', encoding='utf-8') as f:
        f.write("(kicad_symbol_lib\n")
        f.write(f"\t{version_line}\n")
        f.write(f"\t{generator_line}\n")
        for block in symbol_blocks:
            f.write(block)
            f.write("\n")
        f.write(")\n")

def get_symbols_in_file(filepath):
    symbols = []
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            
            depth = 0
            in_quotes = False
            escape = False
            
            i = 0
            n = len(content)
            while i < n:
                c = content[i]
                if escape:
                    escape = False
                    i += 1
                    continue
                if c == '\\':
                    escape = True
                    i += 1
                    continue
                if c == '"':
                    in_quotes = not in_quotes
                    i += 1
                    continue
                if not in_quotes:
                    if c == '(':
                        depth += 1
                        if depth == 2:
                            if content[i+1:i+8] == "symbol ":
                                name_start = content.find('"', i+8)
                                if name_start != -1:
                                    name_end = content.find('"', name_start + 1)
                                    if name_end != -1:
                                        symbols.append(content[name_start+1:name_end])
                    elif c == ')':
                        depth -= 1
                i += 1
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
        
    print(f"Scanning for symbol directories in {kicad_sym_dir}...")
    
    temp_combined_dir = "/home/jnd/qt_projects/viospice/scratch/kicad_combined"
    os.makedirs(temp_combined_dir, exist_ok=True)
    
    symbol_files = []
    for d in os.listdir(kicad_sym_dir):
        if d.endswith(".kicad_symdir"):
            category = d[:-13]
            if args.category and args.category.lower() not in category.lower():
                continue
                
            symdir_path = os.path.join(kicad_sym_dir, d)
            combined_file = os.path.join(temp_combined_dir, f"{category}.kicad_sym")
            
            print(f"Combining {d} into a single library file...")
            combine_symdir_to_file(symdir_path, combined_file)
            symbol_files.append((combined_file, category))
                
    print(f"Found and combined {len(symbol_files)} libraries.")
    
    # 1. Discover all symbols to convert
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
        
    # Clean up temporary combined files
    shutil.rmtree(temp_combined_dir, ignore_errors=True)
        
    print(f"Conversion complete: Successfully converted {success_count}/{len(tasks)} symbols.")

if __name__ == "__main__":
    main()
