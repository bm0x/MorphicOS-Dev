#!/usr/bin/env python3
import sys
import struct
import os

# Morphic Package Format (.mpk)
# Header Structure (Little Endian):
# Magic: 4 bytes "MPK1"
# Manifest Offset: 4 bytes
# Code Offset: 4 bytes
# Code Size: 4 bytes
# Assets Offset: 4 bytes
# Assets Size: 4 bytes
# ... Padding ...

def create_mpk(binary_path, assets_paths, output_path):
    HEADER_SIZE = 64
    MAGIC = b'MPK1'
    
    # Read Binary
    try:
        with open(binary_path, 'rb') as f:
            code_data = f.read()
    except FileNotFoundError:
        print(f"Error: Binary file {binary_path} not found.")
        return

    # Read Assets
    assets_data = b''
    # Simple asset concatenation for now. 
    # In future, a mini-directory at start of assets block would be needed.
    # For now, we assume fixed assets or specific read logic in app.
    for asset in assets_paths:
        try:
            with open(asset, 'rb') as f:
                assets_data += f.read()
        except FileNotFoundError:
            print(f"Warning: Asset {asset} not found.")

    manifest_offset = HEADER_SIZE # Manifest effectively starts after header?
    # Actually, let's keep it simple: Header -> Code -> Assets
    # We skip explicit manifest file for this MVP if not provided, or stub it.
    
    code_offset = HEADER_SIZE
    code_size = len(code_data)
    
    assets_offset = code_offset + code_size
    assets_size = len(assets_data)
    
    # Pack Header
    # 4s = char[4] magic
    # I = uint32 (4 bytes)
    header = struct.pack('<4sIIIII', 
                         MAGIC, 
                         0, # Manifest offset (0 = none for now)
                         code_offset, 
                         code_size, 
                         assets_offset, 
                         assets_size)
    
    # Pad header to 64 bytes
    header += b'\x00' * (HEADER_SIZE - len(header))
    
    print(f"Creating Package: {output_path}")
    print(f"  Code Size: {code_size} bytes")
    print(f"  Assets Size: {assets_size} bytes")
    
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(code_data)
        f.write(assets_data)
        
    print("Done.")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: ./mpk_pack.py <output.mpk> <binary.bin> [asset1] [asset2] ...")
        sys.exit(1)
        
    output = sys.argv[1]
    binary = sys.argv[2]
    assets = sys.argv[3:]
    
    create_mpk(binary, assets, output)
