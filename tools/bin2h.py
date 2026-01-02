#!/usr/bin/env python3
import sys
import os

def main():
    if len(sys.argv) < 4:
        print("Usage: bin2h.py <input_binary> <output_header> <array_name>")
        sys.exit(1)
        
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    array_name = sys.argv[3]
    
    try:
        with open(input_path, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: Input file {input_path} not found.")
        # Create dummy if missing to avoid build break (for clean builds where mpk is made later?)
        # No, mpk must exist.
        sys.exit(1)
        
    with open(output_path, 'w') as f:
        f.write(f"#pragma once\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"extern const uint32_t {array_name}_size = {len(data)};\n")
        f.write(f"extern const uint8_t {array_name}_data[] = {{\n")

        
        # Write bytes
        for i, byte in enumerate(data):
            f.write(f"0x{byte:02X},")
            if (i + 1) % 16 == 0:
                f.write("\n")
        
        f.write("\n};\n")
        
    print(f"Generated {output_path} from {input_path} ({len(data)} bytes).")

if __name__ == "__main__":
    main()
