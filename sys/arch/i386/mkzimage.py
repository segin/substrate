#!/usr/bin/env python3
import sys
import struct
import os

def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <header_bin> <kernel_raw> <output_zimage>")
        sys.exit(1)

    header_path = sys.argv[1]
    kernel_path = sys.argv[2]
    output_path = sys.argv[3]

    try:
        with open(header_path, 'rb') as f:
            header_data = bytearray(f.read())
    except FileNotFoundError:
        print(f"Error: Header file not found: {header_path}")
        sys.exit(1)

    try:
        with open(kernel_path, 'rb') as f:
            kernel_data = f.read()
    except FileNotFoundError:
        print(f"Error: Kernel file not found: {kernel_path}")
        sys.exit(1)

    # Calculate kernel size in 16-byte paragraphs
    kernel_size = len(kernel_data)
    syssize = (kernel_size + 15) // 16
    print(f"Kernel size: {kernel_size} bytes")
    print(f"Syssize (paragraphs): {syssize}")

    # Pad header to 512-byte boundary
    # Setup code includes the boot sector (first 512 bytes)
    # If header_data is e.g. 564 bytes, it spans 2 sectors.
    # Total setup size should be multiple of 512.
    header_len = len(header_data)
    padded_len = (header_len + 511) // 512 * 512
    if padded_len < 1024:
        padded_len = 1024 # Minimum 2 sectors (Boot + Setup)

    padding_needed = padded_len - header_len
    header_data.extend(b'\x00' * padding_needed)

    # Calculate setup_sects
    # setup_sects is the number of 512-byte sectors following the boot sector (sector 0).
    # Total sectors = padded_len / 512
    # setup_sects = Total sectors - 1
    total_sectors = padded_len // 512
    setup_sects = total_sectors - 1
    print(f"Total setup sectors: {total_sectors}")
    print(f"setup_sects: {setup_sects}")

    # Patch setup_sects at offset 0x1F1 (1 byte)
    # 0x1F1 is 497
    if len(header_data) <= 0x1F1:
        print("Error: Header too short to contain setup_sects")
        sys.exit(1)

    header_data[0x1F1] = setup_sects

    # Patch syssize at offset 0x1F4 (4 bytes, little endian)
    # 0x1F4 is 500
    if len(header_data) <= 0x1F4 + 3:
         print("Error: Header too short to contain syssize")
         sys.exit(1)

    struct.pack_into('<I', header_data, 0x1F4, syssize)

    # Write output
    with open(output_path, 'wb') as f:
        f.write(header_data)
        f.write(kernel_data)

    print(f"Wrote {output_path} (Header: {len(header_data)} bytes, Kernel: {len(kernel_data)} bytes)")

if __name__ == '__main__':
    main()
