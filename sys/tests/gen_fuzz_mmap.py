#!/usr/bin/env python3
"""
Fuzzing test generator for mmap operations
"""

import random

def generate_mmap_fuzz(n_ops=500):
    """Generate random mmap/munmap/mprotect sequence"""
    ops = []
    active_maps = []
    map_id = 0
    
    for _ in range(n_ops):
        choice = random.random()
        
        if choice < 0.5 or not active_maps:
            # mmap
            size = random.choice([4096, 8192, 16384, 65536, 1024*1024])
            prot = random.choice([
                'PROT_READ',
                'PROT_WRITE', 
                'PROT_READ | PROT_WRITE',
                'PROT_READ | PROT_WRITE | PROT_EXEC'
            ])
            flags = random.choice([
                'MAP_PRIVATE | MAP_ANONYMOUS',
                'MAP_SHARED | MAP_ANONYMOUS'
            ])
            
            ops.append(('mmap', map_id, size, prot, flags))
            active_maps.append((map_id, size))
            map_id += 1
            
        elif choice < 0.8:
            # munmap
            victim_id, victim_size = random.choice(active_maps)
            ops.append(('munmap', victim_id, victim_size))
            active_maps = [(i, s) for i, s in active_maps if i != victim_id]
            
        else:
            # mprotect
            victim_id, victim_size = random.choice(active_maps)
            prot = random.choice(['PROT_READ', 'PROT_READ | PROT_WRITE'])
            ops.append(('mprotect', victim_id, victim_size, prot))
    
    # Cleanup
    for map_id, size in active_maps:
        ops.append(('munmap', map_id, size))
    
    return ops

def generate_c_code(ops):
    code = """/*
 * Auto-generated fuzzing test for mmap
 */

#include "../vm/vm_area.h"
#include <sys/mman.h>
#include "../kern/console.h"

void run_mmap_fuzz_test(void) {
    kprint("\\n=== MMAP Fuzzing Test ===\\n");
    kprint("Testing random mmap/munmap/mprotect sequences...\\n");
    
    void *maps[5000] = {NULL};
    int ops = 0;
    
"""
    
    for i, op in enumerate(ops):
        if op[0] == 'mmap':
            _, map_id, size, prot, flags = op
            code += f"    // mmap {map_id}\n"
            code += f"    maps[{map_id}] = sys_mmap(NULL, {size}, {prot}, {flags}, -1, 0);\n"
            code += f"    ops++;\n"
        elif op[0] == 'munmap':
            _, map_id, size = op
            code += f"    // munmap {map_id}\n"
            code += f"    if (maps[{map_id}]) {{\n"
            code += f"        sys_munmap(maps[{map_id}], {size});\n"
            code += f"        maps[{map_id}] = NULL;\n"
            code += f"    }}\n"
            code += f"    ops++;\n"
        else:  # mprotect
            _, map_id, size, prot = op
            code += f"    // mprotect {map_id}\n"
            code += f"    if (maps[{map_id}]) {{\n"
            code += f"        sys_mprotect(maps[{map_id}], {size}, {prot});\n"
            code += f"    }}\n"
            code += f"    ops++;\n"
        
        if i % 50 == 0:
            code += '    kprint(".");\n'
    
    code += """
    kprint("\\nCompleted ");
    kprint(" operations without crash\\n");
    kprint("PASS\\n");
}
"""
    return code

if __name__ == '__main__':
    random.seed(42)
    ops = generate_mmap_fuzz(500)
    code = generate_c_code(ops)
    
    with open('/home/segin/test/sys/tests/fuzz_mmap.c', 'w') as f:
        f.write(code)
    
    print(f"Generated {len(ops)} operations")
    print("Created: fuzz_mmap.c")
