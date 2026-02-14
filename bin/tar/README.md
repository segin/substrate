# tar

This directory contains a POSIX-oriented `tar` implementation for Substrate.

## Build

```sh
make -C bin/tar
```

Host build:

```sh
make -C bin/tar NATIVE_BUILD=1
```

## Supported features

- Modes: create (`-c`), extract (`-x`), list (`-t`), append (`-r`/`--concatenate`), update (`-u`)
- Formats: `--format=ustar` and `--format=pax` (default)
- Streaming: use `-f -` for stdin/stdout streams
- Compression wrappers: `-z`/`-J`/`-j` via external compressors
- Safe extraction: `--safe-extract`, `--strip-components`, `--no-overwrite`, `--keep-directory-symlink`
- Metadata handling: ownership, permissions, mtimes, links, directories
- Incremental snapshots: `--listed-incremental=<snapshot-file>`
- Sparse-aware extraction: zero blocks are reconstructed with hole-preserving seeks when possible

## Notes

- PAX headers are emitted when path or numeric fields overflow ustar limits.
- `append` and `update` require a regular on-disk archive file (not compressed stream).
