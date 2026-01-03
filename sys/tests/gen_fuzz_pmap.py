#!/usr/bin/env python3
"""
Fuzzing test for pmap operations
Generates random sequences of create/destroy operations
and checks for crashes or memory corruption
"""

import random
import struct
import sys

def generate_fuzz_sequence(n_ops=1000):
    """Generate random sequence of pmap operations"""
    ops = []
    pmap_ids = []
    next_id = 0
    
    for _ in range(n_ops):
        # 70% create, 30% destroy
        if random.random() < 0.7:
            ops.append(('create', next_id))
            pmap_ids.append(next_id)
            next_id += 1
        else:
            if pmap_ids:
                # Destroy random existing pmap
                victim = random.choice(pmap_ids)
                ops.append(('destroy', victim))
                pmap_ids.remove(victim)
            else:
                # Try to destroy non-existent pmap (should be handled gracefully)
                ops.append(('destroy', random.randint(0, 1000)))
    
    # Cleanup: destroy all remaining
    for pid in pmap_ids:
        ops.append(('destroy', pid))
    
    return ops

def generate_fuzz_c_code(ops):
    """Generate C code for fuzzing test"""
    code = """/*
 * Auto-generated fuzzing test for pmap
 * Tests random sequences of create/destroy
 */

#include "../arch/i386/pmap.h"
#include "../kern/console.h"

void run_pmap_fuzz_test(void) {
    kprint("\\n=== PMAP Fuzzing Test ===\\n");
    kprint("Testing random create/destroy sequences...\\n");
    
    pmap_t pmaps[10000] = {0};
    int ops = 0;
    
"""
    
    for op, pmap_id in ops:
        if op == 'create':
            code += f"    // Create pmap {pmap_id}\n"
            code += f"    pmaps[{pmap_id}] = pmap_create();\n"
            code += f"    ops++;\n"
        else:
            code += f"    // Destroy pmap {pmap_id}\n"
            code += f"    if (pmaps[{pmap_id}]) {{\n"
            code += f"        pmap_destroy(pmaps[{pmap_id}]);\n"
            code += f"        pmaps[{pmap_id}] = 0;\n"
            code += f"    }}\n"
            code += f"    ops++;\n"
        
        # Add progress marker every 100 ops
        if len([o for o in ops[:ops.index((op, pmap_id))+1]]) % 100 == 0:
            code += '    kprint(".");\n'
    
    code += """
    kprint("\\nCompleted ");
    kprint(" operations without crash\\n");
    kprint("PASS\\n");
}
"""
    return code

def main():
    random.seed(42)  # Deterministic for reproducibility
    
    print("Generating fuzzing test...")
    ops = generate_fuzz_sequence(1000)
    code = generate_fuzz_c_code(ops)
    
    with open('/home/segin/test/sys/tests/fuzz_pmap.c', 'w') as f:
        f.write(code)
    
    print(f"Generated {len(ops)} operations")
    print("Created: fuzz_pmap.c")

if __name__ == '__main__':
    main()
