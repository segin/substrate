#!/usr/bin/env python3
import sys
import re

IDENT_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')

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
    print('')

    symbols = []

    for line in sys.stdin:
        parts = line.split()
        # nm output: <addr> <type> <name>
        if len(parts) >= 3:
            addr_str, type_char, name = parts[0], parts[1], parts[2]

            # Only use global symbols. Local/static symbols don't have stable
            # C names we can reference as pointers in generated C.
            if type_char not in ['T', 'D', 'B', 'R', 'A']:
                continue

            # Filter out mapping symbols or compiler internals if necessary
            if name.startswith('$') or name.startswith('.L'):
                continue
            if not IDENT_RE.match(name):
                continue

            try:
                addr = int(addr_str, 16)
                symbols.append((addr, type_char, name))
            except ValueError:
                pass

    # Sort symbols by address
    symbols.sort(key=lambda x: x[0])

    # Emit extern declarations so table entries use real linked pointers.
    decls = set()
    for _, type_char, name in symbols:
        if type_char == 'T':
            decls.add(f'extern void {name}(void);')
        else:
            decls.add(f'extern char {name}[];')

    for decl in sorted(decls):
        print(decl)
    print('')
    print('struct ksym ksym_table[] = {')

    count = 0
    for _, _, name in symbols:
        # Truncate to 55 chars
        if len(name) > 55:
            name = name[:55]
        print(f'    {{ (uint32_t)(uintptr_t)&{name}, "{name}" }},')
        count += 1

    print('    { 0xFFFFFFFF, "" }')
    print('};')
    print('')
    print(f'int ksym_count = {count};')

if __name__ == '__main__':
    main()
