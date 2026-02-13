#!/usr/bin/env python3
"""
Fuzzing test for pmap operations
Generates random sequences of pmap operations including:
- create/destroy
- enter/remove (map/unmap)
- protect
- extract
"""

import random
import os
import sys

# Pmap constants from sys/arch/i386/pmap.h
VM_PROT_READ  = 0x01
VM_PROT_WRITE = 0x02
VM_PROT_EXEC  = 0x04
VM_PROT_USER  = 0x08
VM_PROT_ALL   = (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC | VM_PROT_USER)

PAGE_SIZE = 4096

class PmapFuzzer:
    def __init__(self, n_ops=1500, seed=42):
        self.n_ops = n_ops
        self.seed = seed
        random.seed(seed)

        # State tracking
        self.pmaps = []       # List of active pmap IDs
        self.mappings = {}    # pmap_id -> set of mapped VAs
        self.next_pmap_id = 0
        self.ops = []         # Generated operations: (type, args...)

    def generate(self):
        """Generate the sequence of operations"""
        for _ in range(self.n_ops):
            # Weighted choice of operation type
            # Bias towards modification operations if pmaps exist
            if not self.pmaps:
                op_type = 'create'
            else:
                r = random.random()
                if r < 0.15:
                    op_type = 'create'
                elif r < 0.25:
                    op_type = 'destroy'
                elif r < 0.55:
                    op_type = 'enter'
                elif r < 0.70:
                    op_type = 'remove'
                elif r < 0.85:
                    op_type = 'protect'
                else:
                    op_type = 'extract'

            self._add_op(op_type)

        # Cleanup: destroy all remaining pmaps
        for pid in list(self.pmaps):
            self.ops.append(('destroy', pid))

        return self.ops

    def _add_op(self, op_type):
        if op_type == 'create':
            pid = self.next_pmap_id
            self.next_pmap_id += 1
            self.pmaps.append(pid)
            self.mappings[pid] = set()
            self.ops.append(('create', pid))

        elif op_type == 'destroy':
            if not self.pmaps:
                return # Should have been handled by caller, but safe check

            # Pick a victim
            pid = random.choice(self.pmaps)
            self.pmaps.remove(pid)
            del self.mappings[pid]
            self.ops.append(('destroy', pid))

        elif op_type == 'enter':
            if not self.pmaps: return
            pid = random.choice(self.pmaps)

            # Generate random page-aligned VA in user space (0 - 3GB)
            # Avoid 0 to be safe (NULL)
            va = random.randint(1, 0xBFFFF) * PAGE_SIZE

            # Generate random PA (simulated)
            pa = random.randint(0x100, 0xFFFFF) * PAGE_SIZE

            prot = random.choice([
                VM_PROT_READ,
                VM_PROT_READ | VM_PROT_WRITE,
                VM_PROT_READ | VM_PROT_EXEC,
                VM_PROT_ALL
            ])

            self.mappings[pid].add(va)
            self.ops.append(('enter', pid, va, pa, prot))

        elif op_type == 'remove':
            if not self.pmaps: return
            pid = random.choice(self.pmaps)

            if not self.mappings[pid]:
                # Try to remove unmapped address occasionally
                va = random.randint(1, 0xBFFFF) * PAGE_SIZE
            else:
                va = random.choice(list(self.mappings[pid]))
                self.mappings[pid].remove(va)

            self.ops.append(('remove', pid, va))

        elif op_type == 'protect':
            if not self.pmaps: return
            pid = random.choice(self.pmaps)

            if not self.mappings[pid]: return

            va = random.choice(list(self.mappings[pid]))
            prot = random.choice([VM_PROT_READ, VM_PROT_ALL])

            self.ops.append(('protect', pid, va, prot))

        elif op_type == 'extract':
            if not self.pmaps: return
            pid = random.choice(self.pmaps)

            if random.random() < 0.8 and self.mappings[pid]:
                va = random.choice(list(self.mappings[pid]))
            else:
                va = random.randint(1, 0xBFFFF) * PAGE_SIZE

            self.ops.append(('extract', pid, va))

    def to_c_code(self):
        """Generate C code from operations"""
        code = [
            "/*",
            " * Auto-generated comprehensive fuzzing test for pmap",
            " * Tests: create, destroy, enter, remove, protect, extract",
            " */",
            "",
            "#include <sys/types.h>",
            "#include \"../arch/i386/pmap.h\"",
            "#include \"../kern/console.h\"",
            "",
            "void run_pmap_fuzz_test(void) {",
            "    kprint(\"\\n=== PMAP Fuzzing Test (Comprehensive) ===\\n\");",
            "    kprint(\"Testing random pmap operations...\\n\");",
            "",
            f"    pmap_t pmaps[{self.next_pmap_id + 1}];",
            "    for (int i = 0; i < " + str(self.next_pmap_id + 1) + "; i++) pmaps[i] = 0;",
            "    int ops_count = 0;",
            ""
        ]
        
        # Efficient loop using enumerate
        for i, op_tuple in enumerate(self.ops):
            op_type = op_tuple[0]

            if op_type == 'create':
                pid = op_tuple[1]
                code.append(f"    // Op {i}: Create pmap {pid}")
                code.append(f"    pmaps[{pid}] = pmap_create();")
                code.append(f"    if (!pmaps[{pid}]) kprint(\"Warning: pmap_create failed for {pid}\\n\");")

            elif op_type == 'destroy':
                pid = op_tuple[1]
                code.append(f"    // Op {i}: Destroy pmap {pid}")
                code.append(f"    if (pmaps[{pid}]) {{")
                code.append(f"        pmap_destroy(pmaps[{pid}]);")
                code.append(f"        pmaps[{pid}] = 0;")
                code.append(f"    }}")

            elif op_type == 'enter':
                _, pid, va, pa, prot = op_tuple
                code.append(f"    // Op {i}: Enter pmap {pid} va={hex(va)} pa={hex(pa)} prot={hex(prot)}")
                code.append(f"    if (pmaps[{pid}]) {{")
                # flags=0
                code.append(f"        pmap_enter(pmaps[{pid}], {va}, {pa}, {prot}, 0);")
                code.append(f"    }}")

            elif op_type == 'remove':
                _, pid, va = op_tuple
                code.append(f"    // Op {i}: Remove pmap {pid} va={hex(va)}")
                code.append(f"    if (pmaps[{pid}]) {{")
                code.append(f"        pmap_remove(pmaps[{pid}], {va});")
                code.append(f"    }}")

            elif op_type == 'protect':
                _, pid, va, prot = op_tuple
                # protect range: just one page
                code.append(f"    // Op {i}: Protect pmap {pid} va={hex(va)}")
                code.append(f"    if (pmaps[{pid}]) {{")
                code.append(f"        pmap_protect(pmaps[{pid}], {va}, {va + PAGE_SIZE}, {prot});")
                code.append(f"    }}")

            elif op_type == 'extract':
                _, pid, va = op_tuple
                code.append(f"    // Op {i}: Extract pmap {pid} va={hex(va)}")
                code.append(f"    if (pmaps[{pid}]) {{")
                code.append(f"        pmap_extract(pmaps[{pid}], {va});")
                code.append(f"    }}")

            code.append("    ops_count++;")

            # Progress marker every 100 ops
            if (i + 1) % 100 == 0:
                 code.append('    kprint(".");')

        code.append("")
        code.append("    kprint(\"\\nCompleted operations without crash\\n\");")
        code.append("    kprint(\"PASS\\n\");")
        code.append("}")

        return "\n".join(code)

def main():
    print("Generating comprehensive fuzzing test...")
    fuzzer = PmapFuzzer(n_ops=2000) # Increased ops for better coverage
    fuzzer.generate()
    code = fuzzer.to_c_code()
    
    output_path = os.path.join(os.path.dirname(__file__) if '__file__' in globals() else '.', 'fuzz_pmap.c')

    with open(output_path, 'w') as f:
        f.write(code)

    print(f"Generated {len(fuzzer.ops)} operations")
    print(f"Created: {output_path}")

if __name__ == '__main__':
    main()
