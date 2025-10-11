#!/usr/bin/env python3
"""
Patch kernel module symbol CRCs to match stock kernel.

This script reads the symbol CRCs from a stock kernel module and patches them
into a newly compiled module, allowing it to load on the stock kernel despite
being built against a different kernel source tree.
"""

import struct
import subprocess
import sys
from typing import Dict, List, Optional, Tuple


def get_module_crcs(module_path: str) -> Dict[str, int]:
    """Extract symbol CRCs from a kernel module using modprobe --dump-modversions."""
    result = subprocess.run(
        ["modprobe", "--dump-modversions", module_path], capture_output=True, text=True
    )

    crcs: Dict[str, int] = {}
    for line in result.stdout.strip().split("\n"):
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) != 2:
            continue
        crc_str, symbol = parts
        crcs[symbol] = int(crc_str, 16)

    return crcs


def find_symbol_in_versions(data: bytes, symbol: str) -> Tuple[int, int]:
    """
    Find a symbol in the __versions section.

    Each entry is 64 bytes:
    - 4 bytes: CRC (little-endian uint32)
    - 60 bytes: symbol name (null-terminated string)

    Returns: (offset, current_crc) or (-1, 0) if not found
    """
    symbol_bytes = symbol.encode("ascii") + b"\x00"

    # Search through the data in 64-byte chunks
    offset = 0
    while offset + 64 <= len(data):
        # Extract name field (bytes 4-64)
        name_field = data[offset + 4 : offset + 64]

        # Check if this entry matches our symbol
        if name_field.startswith(symbol_bytes):
            # Extract current CRC (first 4 bytes, little-endian)
            current_crc = struct.unpack("<I", data[offset : offset + 4])[0]
            return offset, current_crc

        offset += 64

    return -1, 0


def patch_module_crcs(
    target_module: str, stock_module: str, output_module: str
) -> None:
    """Patch symbol CRCs from stock module into target module."""

    print(f"Extracting CRCs from stock module: {stock_module}")
    stock_crcs = get_module_crcs(stock_module)
    print(f"  Found {len(stock_crcs)} symbols in stock module")

    print(f"\nExtracting CRCs from target module: {target_module}")
    target_crcs = get_module_crcs(target_module)
    print(f"  Found {len(target_crcs)} symbols in target module")

    # Find __versions section in target module
    print(f"\nReading target module: {target_module}")
    with open(target_module, "rb") as f:
        module_data = bytearray(f.read())

    # Use readelf to get __versions section offset and size
    result = subprocess.run(
        ["readelf", "-S", target_module], capture_output=True, text=True
    )

    versions_offset: Optional[int] = None
    versions_size: Optional[int] = None
    for line in result.stdout.split("\n"):
        if "__versions" in line:
            # Parse: [23] __versions PROGBITS 00000000 000ed8 000540 00 A 0 0 4
            # Fields: [num] name type addr offset size ...
            parts = line.split()
            # Find __versions in parts, then offset is +3, size is +4
            idx = parts.index("__versions")
            versions_offset = int(parts[idx + 3], 16)
            versions_size = int(parts[idx + 4], 16)
            break

    if versions_offset is None or versions_size is None:
        print("ERROR: Could not find __versions section in target module")
        sys.exit(1)

    # Type narrowing: assert that these are not None after the check
    assert versions_offset is not None
    assert versions_size is not None

    print(
        f"Found __versions section at offset 0x{versions_offset:x}, size 0x{versions_size:x}"
    )

    # Extract __versions section
    versions_data = bytes(
        module_data[versions_offset : versions_offset + versions_size]
    )

    # Patch CRCs
    patches_applied = 0
    mismatches: List[Tuple[str, int, int]] = []

    print("\nPatching CRCs:")
    for symbol, stock_crc in stock_crcs.items():
        # Find symbol in target module's __versions section
        symbol_offset, current_crc = find_symbol_in_versions(versions_data, symbol)

        if symbol_offset == -1:
            print(f"  WARNING: Symbol '{symbol}' not found in target module")
            continue

        if current_crc == stock_crc:
            print(f"  {symbol}: CRC already matches (0x{stock_crc:08x})")
            continue

        # Patch the CRC
        absolute_offset = versions_offset + symbol_offset
        new_crc_bytes = struct.pack("<I", stock_crc)
        module_data[absolute_offset : absolute_offset + 4] = new_crc_bytes

        print(f"  {symbol}: 0x{current_crc:08x} -> 0x{stock_crc:08x}")
        mismatches.append((symbol, current_crc, stock_crc))
        patches_applied += 1

    # Write patched module
    print(f"\nWriting patched module: {output_module}")
    with open(output_module, "wb") as f:
        f.write(module_data)

    print("\nSummary:")
    print(f"  Total symbols in stock: {len(stock_crcs)}")
    print(f"  Total symbols in target: {len(target_crcs)}")
    print(f"  CRCs patched: {patches_applied}")
    print(f"  CRCs already matching: {len(stock_crcs) - patches_applied}")

    if patches_applied > 0:
        print(f"\nPatch complete! Module saved as: {output_module}")
    else:
        print("\nNo patches needed - all CRCs already match!")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(
            "Usage: patch_module_crcs.py <target_module.ko> <stock_module.ko> <output_module.ko>"
        )
        sys.exit(1)

    target_module = sys.argv[1]
    stock_module = sys.argv[2]
    output_module = sys.argv[3]

    patch_module_crcs(target_module, stock_module, output_module)
