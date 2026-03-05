#!/usr/bin/env python3
import sys

def main():
    if len(sys.argv) != 1:
        print("Usage: nm kernel.tmp | python3 mkksyms.py > ksyms_table.c")
        sys.exit(1)

    print('#include <stdint.h>')
    print('')
    print('struct ksym {')
    print('    uint32_t addr;')
    print('    char name[56];')
    print('};')
    print('')
    print('struct ksym ksym_table[] = {')

    count = 0
    symbols = []

    for line in sys.stdin:
        parts = line.split()
        # nm output: <addr> <type> <name>
        if len(parts) >= 3:
            addr_str, type_char, name = parts[0], parts[1], parts[2]

            # Interested in Text/Data/Bss/ROData
            if type_char.lower() not in ['t', 'd', 'b', 'r', 'a']:
                continue

            # Filter out mapping symbols or compiler internals if necessary
            if name.startswith('$') or name.startswith('.L'):
                continue

            try:
                addr = int(addr_str, 16)
                symbols.append((addr, name))
            except ValueError:
                pass

    # Sort symbols by address
    symbols.sort(key=lambda x: x[0])

    for addr, name in symbols:
        # Truncate to 55 chars
        if len(name) > 55:
            name = name[:55]
        print(f'    {{ 0x{addr:08x}, "{name}" }},')
        count += 1

    print('    { 0xFFFFFFFF, "" }')
    print('};')
    print('')
    print(f'int ksym_count = {count};')

if __name__ == '__main__':
    main()
