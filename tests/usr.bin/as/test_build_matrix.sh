#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TMP=${TMPDIR:-/tmp}/as-build-matrix-$$
DEST="$TMP/dest"
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

# Recursive Makefile host build path.
make -C "$ROOT/usr.bin/as" NATIVE_BUILD=1

# Build-tree multi-call aliases.
for link in as.x86 as.x64 arm-as aarch64-as; do
    test -L "$ROOT/usr.bin/as/$link"
    test "$(readlink "$ROOT/usr.bin/as/$link")" = "as"
done

# install -> $(DESTDIR)/usr/bin/as plus aliases.
make -C "$ROOT/usr.bin/as" NATIVE_BUILD=1 DESTDIR="$DEST" install

test -x "$DEST/usr/bin/as"
for link in as.x86 as.x64 arm-as aarch64-as; do
    test -L "$DEST/usr/bin/$link"
    test "$(readlink "$DEST/usr/bin/$link")" = "as"
done

# libelfobj static dependency contract.
grep -q 'LDADD += ../../usr.lib/elfobj/libelfobj.a' "$ROOT/usr.bin/as/Makefile"
grep -q '\$(PROG): ../../usr.lib/elfobj/libelfobj.a' "$ROOT/usr.bin/as/Makefile"
test -f "$ROOT/usr.lib/elfobj/libelfobj.a"

echo "ok: build matrix"
