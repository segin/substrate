# FAT16/32 Filesystem Specification

## Overview
The FAT driver provides compatibility with standard MS-DOS and Windows formatted storage media. It supports both FAT16 and FAT32 variants.

## Implementation
- **BPB (BIOS Parameter Block):** Located in the boot sector (sector 0), contains geometry and metadata.
- **FAT (File Allocation Table):** A linked list of clusters representing file data.
- **Cluster Chaining:** `fat_get_next_cluster()` reads the FAT to find the next part of a file.
- **Directory Entries:** 32-byte structures containing 8.3 filenames, attributes, and starting cluster.

## API
### `uint32_t fat_get_next_cluster(uint32_t cluster)`
Retrieves the next cluster index in a chain.

## Constraints
- LFN (Long File Names) are not yet supported.
- Write support is not yet implemented.
- Cluster calculation currently stubbed.
