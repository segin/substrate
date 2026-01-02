# Swap Subsystem Specification

## Overview
The Swap Subsystem provides a mechanism to move physical pages to a secondary backing store (disk) when physical memory is exhausted. This allows the system to support a larger virtual address space than physical RAM.

## Design
- **Pager Architecture:** The `swap_pager` handles the movement of pages between the VM Object layer and the swap device.
- **Backing Store:** Support for partition-based swap (linear block access) and file-based swap.
- **Metadata:** A swap map tracks which disk blocks are allocated to which VM objects/pages.
- **Replacement Policy:** A clock or LRU algorithm identifies "inactive" pages to be swapped out.

## API
### `void swap_init(void)`
Initializes the swap metadata and registers the swap pager.

### `int swap_out(vm_page_t *m)`
Moves a physical page to the swap device and frees the physical frame.

### `int swap_in(vm_page_t *m)`
Retrieves a page from the swap device into a newly allocated physical frame.

## Constraints
- Requires functional block device drivers (IDE/SATA/NVMe).
- Initial implementation will use a simple first-fit allocation for swap blocks.
