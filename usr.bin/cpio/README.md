# cpio

`usr.bin/cpio` is a standalone POSIX-style `cpio(1)` utility for Substrate.

## Implemented modes
- `-o` copy-out (create archive from newline-separated path list on stdin)
- `-i` copy-in (extract/list from archive)
- `-p` pass-through (copy newline-separated path list into target directory)

## Formats
- `newc` (`070701`)
- `odc` (`070707` ASCII)
- `bin` (old binary)

Select using `-H format` or `--format=format`.

## Safety options
- `--safe-extract`
- `--no-absolute-paths`
- `--no-overwrite`
- `--numeric-owner`

## Exit codes
- `0` success
- `1` non-fatal/partial errors
- `2` fatal errors (invalid arguments, unrecoverable archive parse/write)

## Build
```sh
make -C usr.bin/cpio
```

## Quick examples
```sh
# Create
find etc -print | usr.bin/cpio/cpio -o -H newc -F etc.cpio

# List
usr.bin/cpio/cpio -i -t -F etc.cpio

# Extract safely
mkdir out
(cd out && ../usr.bin/cpio/cpio -i -d --safe-extract -F ../etc.cpio)
```
