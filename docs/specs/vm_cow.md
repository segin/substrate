# Copy-on-Write (CoW) Specification

## Overview
Copy-on-Write (CoW) is an optimization that allows multiple processes to share the same physical page until one of them attempts to modify it. At that point, a new private copy of the page is created.

## Design
- **Mechanism:**
    1. Pages are initially mapped as Read-Only in the hardware page tables, even if the VM Map allows Write access.
    2. When a Write fault occurs, `vm_fault` detects that the page is shared.
    3. A new physical page is allocated.
    4. The contents of the original page are copied to the new page.
    5. The hardware mapping is updated to point to the new page with Write permissions enabled.
- **Reference Counting:** Physical pages (`vm_page_t`) or VM Objects must track their reference counts to identify when a page is truly shared.

## API Integration
- `vm_fault()`: logic updated to handle `VM_PROT_WRITE` on a page with a reference count > 1.

## Constraints
- Requires functional `pmap_enter` and `vm_page_alloc`.
- In the initial implementation, CoW will be triggered for any write to an object with `ref_count > 1`.
