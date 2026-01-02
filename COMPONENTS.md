# TestUnix Component Registry

This database tracks the engineering artifacts and verification status for every system component.

| Component | Status | Spec | Unit Test | Property Test | Fuzz Test |
|-----------|--------|------|-----------|---------------|-----------|
| **PMM (i386)** | Stable | [Doc](docs/specs/pmm.md) | [tests/unit/vm/test_pmm.c](tests/unit/vm/test_pmm.c) | No | No |
| **PMAP (i386)** | Stable | [Doc](docs/specs/pmap.md) | [tests/unit/arch/test_pmap_i386.c](tests/unit/arch/test_pmap_i386.c) | No | No |
| **PMAP (x86_64)** | Stable | [Doc](docs/specs/pmap.md) | [tests/unit/arch/test_pmap_x64.c](tests/unit/arch/test_pmap_x64.c) | No | No |
| **VM Page** | Stable | [Doc](docs/specs/vm_page.md) | [tests/unit/vm/test_page.c](tests/unit/vm/test_page.c) | No | No |
| **VM Map** | Stable | [Doc](docs/specs/vm_map.md) | [tests/unit/vm/test_map.c](tests/unit/vm/test_map.c) | No | No |
| **VM Object** | Stable | [Doc](docs/specs/vm_object.md) | [tests/unit/vm/test_object.c](tests/unit/vm/test_object.c) | No | No |
| **Zone Allocator** | Stable | [Doc](docs/specs/vm_zone.md) | [tests/unit/vm/test_zone.c](tests/unit/vm/test_zone.c) | No | No |
| **Fault Handler** | Stable | [Doc](docs/specs/vm_fault.md) | [tests/unit/vm/test_fault.c](tests/unit/vm/test_fault.c) | No | No |
| **Kmem** | Stable | [Doc](docs/specs/vm_kmem.md) | [tests/unit/vm/test_kmem.c](tests/unit/vm/test_kmem.c) | **Pending** | **Pending** |
