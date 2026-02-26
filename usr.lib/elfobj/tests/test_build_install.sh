#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
ELFDIR="$TOP/usr.lib/elfobj"
EXDIR="$ELFDIR/examples"
DEST=$(mktemp -d "${TMPDIR:-/tmp}/elfobj-install.XXXXXX")

trap 'rm -rf "$DEST"' EXIT INT TERM

make -C "$ELFDIR" clean all >/dev/null
make -C "$ELFDIR" install DESTDIR="$DEST" >/dev/null

test -f "$DEST/usr/lib/libelfobj.a"
test -f "$DEST/usr/include/elfobj.h"
test -f "$DEST/usr/lib/pkgconfig/elfobj.pc"

make -C "$EXDIR" clean all >/dev/null
(cd "$EXDIR" && ./create_minimal)
(cd "$EXDIR" && ./add_symbol_reloc)
(cd "$EXDIR" && ./merge_two)
(cd "$EXDIR" && ./inspect_symtab example_minimal.o >/dev/null)

test -f "$EXDIR/example_minimal.o"
test -f "$EXDIR/example_reloc.o"
test -f "$EXDIR/example_merge.o"

make -C "$EXDIR" clean >/dev/null
rm -f "$EXDIR"/example_minimal.o "$EXDIR"/example_reloc.o "$EXDIR"/example_merge.o
exit 0
