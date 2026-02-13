# pax(1) for Substrate

`usr.bin/pax` provides a standalone POSIX-style archive interchange utility.

## Implemented capabilities

- Read/list/extract (`-r`) and write/create (`-w`) archives.
- Copy mode (`-r -w`) for file tree copies.
- Archive selection with `-f` and output format selection with `-x pax|ustar|cpio`.
- PAX extended headers are generated and parsed for long names and numeric overflow.
- Reads/writes ustar and cpio newc for interoperability.
- Substitution rules (`-s`) are applied in command-line order.
- Path safety defaults to `--no-absolute-paths` style extraction behavior.
- Symlink behavior controls (`-L`) and overwrite suppression (`-n`/`-k`).
- Preservation controls (`-p`) for mode/time/owner restoration.

## Build

```sh
make -C usr.bin/pax
```

Host build:

```sh
make -C usr.bin/pax NATIVE_BUILD=1
```

## Interop quick examples

```sh
# create pax archive
./usr.bin/pax/pax -w -x pax -f out.pax dir

# extract to current directory
./usr.bin/pax/pax -r -f out.pax

# convert ustar -> pax
./usr.bin/pax/pax -r -f old.tar
./usr.bin/pax/pax -w -x pax -f new.pax extracted-tree
```
