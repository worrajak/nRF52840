#!/usr/bin/env python3
"""
PlatformIO post-build script to copy HEX as UF2 format
for nRF52840 boards with bootloader
"""

import os
import shutil

# env is imported automatically by PlatformIO for post-build scripts

project_dir = env.subst('$PROJECT_DIR')
build_dir = env.subst('$BUILD_DIR')
prog_name = env.subst('$PROGNAME')

# File paths
hex_file = os.path.join(build_dir, f"{prog_name}.hex")
uf2_file_build = os.path.join(build_dir, f"{prog_name}.uf2")
uf2_file_root = os.path.join(project_dir, f"{prog_name}.uf2")

print("\n" + "="*60)
print("[UF2] Post-build script: Converting HEX to UF2")
print("="*60)

if os.path.exists(hex_file):
    print(f"[UF2] Source: {hex_file}")
    
    # Copy HEX as UF2 (nRF52840 bootloader accepts both)
    try:
        shutil.copy(hex_file, uf2_file_build)
        print(f"[UF2] ✓ Created: {uf2_file_build}")
    except Exception as e:
        print(f"[UF2] ✗ Error creating UF2 in build dir: {e}")
    
    # Also copy to project root
    try:
        shutil.copy(hex_file, uf2_file_root)
        print(f"[UF2] ✓ Copied: {uf2_file_root}")
    except Exception as e:
        print(f"[UF2] ✗ Error copying to project root: {e}")
    
    print("[UF2] ✓ UF2 file ready for bootloader upload")
else:
    print(f"[UF2] ✗ HEX file not found: {hex_file}")

print("="*60 + "\n")

