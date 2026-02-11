# PAX - POSIX Archive Interchange

This is a clean-room implementation of the POSIX `pax` utility for Substrate OS.
It supports `ustar` and `cpio` formats, along with the PAX extended header format.

## Features

- **Read/Write/List/Copy/Append** modes as per POSIX.
- **ustar** and **cpio** (odc/newc) support.
- **PAX** extended headers (stubbed).
- **Substitution** (`-s`) via regex (stubbed).
- **Directory Traversal** and safe file handling.

## Build

    make

## Usage

    pax -w -f archive.tar file1 file2
    pax -r -f archive.tar
    pax -rw src_dir dest_dir

## Status

- Core infrastructure: Complete
- Format handling: Basic ustar/cpio identification and parsing.
- I/O: Buffered I/O stubs.
- Regex: Stubs.
- Traversal: Non-recursive argument walking.
