# TestUnix Component Registry

This database tracks the engineering artifacts and verification status for every system component.

| Component | Status | Spec | Unit Test | Property Test | Fuzz Test |
|-----------|--------|------|-----------|---------------|-----------|
| **PMM (i386)** | Stable | [Doc](docs/specs/pmm.md) | [tests/unit/vm/test_pmm.c](tests/unit/vm/test_pmm.c) | [tests/unit/vm/prop_pmm.c](tests/unit/vm/prop_pmm.c) | [tests/fuzz/fuzz_pmm.c](tests/fuzz/fuzz_pmm.c) |
| **PMAP (i386)** | Stable | [Doc](docs/specs/pmap.md) | [tests/unit/arch/test_pmap_i386.c](tests/unit/arch/test_pmap_i386.c) | [tests/unit/arch/prop_pmap_i386.c](tests/unit/arch/prop_pmap_i386.c) | [tests/fuzz/fuzz_pmap_i386.c](tests/fuzz/fuzz_pmap_i386.c) |
| **PMAP (x86_64)** | Stable | [Doc](docs/specs/pmap.md) | [tests/unit/arch/test_pmap_x64.c](tests/unit/arch/test_pmap_x64.c) | [tests/unit/arch/prop_pmap_x64.c](tests/unit/arch/prop_pmap_x64.c) | [tests/fuzz/fuzz_pmap_x64.c](tests/fuzz/fuzz_pmap_x64.c) |
| **VM Page** | Stable | [Doc](docs/specs/vm_page.md) | [tests/unit/vm/test_page.c](tests/unit/vm/test_page.c) | [tests/unit/vm/prop_vm_page.c](tests/unit/vm/prop_vm_page.c) | [tests/fuzz/fuzz_vm_page.c](tests/fuzz/fuzz_vm_page.c) |
| **VM Entries** | Stable | [Doc](docs/specs/vm_map.md) | [tests/unit/vm/test_map.c](tests/unit/vm/test_map.c) | [tests/unit/vm/prop_vm_map.c](tests/unit/vm/prop_vm_map.c) | [tests/fuzz/fuzz_vm_map.c](tests/fuzz/fuzz_vm_map.c) |
| **VM Object** | Stable | [Doc](docs/specs/vm_object.md) | [tests/unit/vm/test_object.c](tests/unit/vm/test_object.c) | [tests/unit/vm/prop_vm_object.c](tests/unit/vm/prop_vm_object.c) | [tests/fuzz/fuzz_vm_object.c](tests/fuzz/fuzz_vm_object.c) |
| **Zone Allocator** | Stable | [Doc](docs/specs/vm_zone.md) | [tests/unit/vm/test_zone.c](tests/unit/vm/test_zone.c) | [tests/unit/vm/prop_vm_zone.c](tests/unit/vm/prop_vm_zone.c) | [tests/fuzz/fuzz_vm_zone.c](tests/fuzz/fuzz_vm_zone.c) |
| **Fault Handler** | Stable | [Doc](docs/specs/vm_fault.md) | [tests/unit/vm/test_fault.c](tests/unit/vm/test_fault.c) | [tests/unit/vm/prop_vm_fault.c](tests/unit/vm/prop_vm_fault.c) | [tests/fuzz/fuzz_vm_fault.c](tests/fuzz/fuzz_vm_fault.c) |
| **Kmem** | Stable | [Doc](docs/specs/vm_kmem.md) | [tests/unit/vm/test_kmem.c](tests/unit/vm/test_kmem.c) | [tests/unit/vm/prop_vm_kmem.c](tests/unit/vm/prop_vm_kmem.c) | [tests/fuzz/fuzz_vm_kmem.c](tests/fuzz/fuzz_vm_kmem.c) |
| **GDT/TSS (i386)** | Stable | [Doc](docs/specs/arch_gdt.md) | [tests/unit/arch/test_gdt.c](tests/unit/arch/test_gdt.c) | [tests/unit/arch/prop_arch_gdt.c](tests/unit/arch/prop_arch_gdt.c) | [tests/fuzz/fuzz_arch_gdt.c](tests/fuzz/fuzz_arch_gdt.c) |
| **Exceptions (i386)** | Stable | [Doc](docs/specs/arch_exceptions.md) | [tests/unit/arch/test_exceptions.c](tests/unit/arch/test_exceptions.c) | [tests/unit/arch/prop_arch_exceptions.c](tests/unit/arch/prop_arch_exceptions.c) | [tests/fuzz/fuzz_arch_exceptions.c](tests/fuzz/fuzz_arch_exceptions.c) |
| **CoW** | Beta | [Doc](docs/specs/vm_cow.md) | [tests/unit/vm/test_cow.c](tests/unit/vm/test_cow.c) | [tests/unit/vm/prop_vm_cow.c](tests/unit/vm/prop_vm_cow.c) | [tests/fuzz/fuzz_vm_cow.c](tests/fuzz/fuzz_vm_cow.c) |
| **Swap** | Beta | [Doc](docs/specs/vm_swap.md) | [tests/unit/vm/test_swap.c](tests/unit/vm/test_swap.c) | [tests/unit/vm/prop_vm_swap.c](tests/unit/vm/prop_vm_swap.c) | [tests/fuzz/fuzz_vm_swap.c](tests/fuzz/fuzz_vm_swap.c) |
| **User Memory** | Stable | [Doc](docs/specs/user_memory.md) | [tests/unit/vm/test_user_memory.c](tests/unit/vm/test_user_memory.c) | [tests/unit/vm/prop_user_memory.c](tests/unit/vm/prop_user_memory.c) | [tests/fuzz/fuzz_mmap.c](tests/fuzz/fuzz_mmap.c) |
