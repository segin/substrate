#!/bin/sh
# The names the assembler is installed under.  The Makefile makes four
# links to it beside it and in $(BINDIR) -- as.x86, as.x64, arm-as,
# aarch64-as -- and the assembler must run under each.
#
# This test used to run `make NATIVE_BUILD=1` and `make install` in the
# source directory, which left a host binary where the target's is
# built, and to look in the Makefile for lines it has not had since the
# object library became a shared one.  It builds nothing now: it reads
# the rules for the links, and runs the assembler it is given under
# each name.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS=${AS:-"$ROOT/usr.bin/as/as"}
case $AS in /*) ;; *) AS=$(pwd)/$AS ;; esac
TMP=${TMPDIR:-/tmp}/as-build-matrix-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

mk="$ROOT/usr.bin/as/Makefile"
for link in as.x86 as.x64 arm-as aarch64-as; do
    # shellcheck disable=SC2016
    grep -q "ln -sf \$(PROG) $link\$" "$mk" ||
        { echo "FAIL: the Makefile does not link $link beside the assembler"; exit 1; }
    # shellcheck disable=SC2016
    grep -q "ln -sf as \$(BINDIR)/$link\$" "$mk" ||
        { echo "FAIL: the Makefile does not install $link"; exit 1; }
done

printf '\t.text\n\tnop\n' > "$TMP/nop.s"
for link in as.x86 as.x64 arm-as aarch64-as; do
    ln -s "$AS" "$TMP/$link"
    "$TMP/$link" -32 -o "$TMP/$link.o" "$TMP/nop.s" ||
        { echo "FAIL: the assembler does not run as $link"; exit 1; }
    [ "$(objcopy -O binary --only-section=.text "$TMP/$link.o" /dev/stdout | od -An -tx1 | tr -d ' \n')" = 90 ] ||
        { echo "FAIL: as $link, nop is not the byte 90"; exit 1; }
done

echo "ok: build matrix"
