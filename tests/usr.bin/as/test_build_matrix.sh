#!/bin/sh
# The names the assembler is installed under, and the ones it is not.
#
# The Makefile makes two links to it beside it and in $(BINDIR), as.x86
# and as.x64, and the assembler must run under each.  It made two more,
# arm-as and aarch64-as, for machines it cannot assemble for: run as
# aarch64-as it wrote `ret` as the byte C3 into an x86-64 object and
# exited 0.  Those are not made, and under such a name it refuses.
#
# This test builds nothing: it reads the rules for the links, and runs
# the assembler it is given under each name.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS=${AS:-"$ROOT/usr.bin/as/as"}
case $AS in /*) ;; *) AS=$(pwd)/$AS ;; esac
TMP=${TMPDIR:-/tmp}/as-build-matrix-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

mk="$ROOT/usr.bin/as/Makefile"
for link in as.x86 as.x64; do
    # shellcheck disable=SC2016
    grep -q "ln -sf \$(PROG) $link\$" "$mk" ||
        { echo "FAIL: the Makefile does not link $link beside the assembler"; exit 1; }
    # shellcheck disable=SC2016
    grep -q "ln -sf as \$(BINDIR)/$link\$" "$mk" ||
        { echo "FAIL: the Makefile does not install $link"; exit 1; }
done
if grep -q 'arm-as\|aarch64-as' "$mk" "$ROOT/build-rootfs.sh"; then
    echo "FAIL: the build installs the assembler under the name of one for ARM"
    exit 1
fi

printf '\t.text\n\tnop\n' > "$TMP/nop.s"
for link in as.x86 as.x64; do
    ln -s "$AS" "$TMP/$link"
    "$TMP/$link" -32 -o "$TMP/$link.o" "$TMP/nop.s" ||
        { echo "FAIL: the assembler does not run as $link"; exit 1; }
    [ "$(objcopy -O binary --only-section=.text "$TMP/$link.o" /dev/stdout | od -An -tx1 | tr -d ' \n')" = 90 ] ||
        { echo "FAIL: as $link, nop is not the byte 90"; exit 1; }
done

# Asked for another machine's code, by name or by option, it fails and
# writes no object.  (These are this assembler's own names and options:
# GNU as for x86 has neither.)
if "$AS" --version 2>/dev/null | grep -qi 'GNU assembler'; then
    echo "ok: build matrix (the names; the refusals are this assembler's own)"
    exit 0
fi
for link in arm-as aarch64-as; do
    ln -s "$AS" "$TMP/$link"
    if "$TMP/$link" -o "$TMP/$link.o" "$TMP/nop.s" 2> "$TMP/err"; then
        echo "FAIL: run as $link, it assembles"; exit 1
    fi
    [ ! -e "$TMP/$link.o" ] || { echo "FAIL: run as $link, it writes an object"; exit 1; }
    grep -q "$link" "$TMP/err" || { echo "FAIL: run as $link, it does not say why: $(head -1 "$TMP/err")"; exit 1; }
done
for opt in --target=aarch64 --target=arm -marm -mthumb -mcpu=cortex-a53 -march=armv8-a -mfpu=neon; do
    rm -f "$TMP/opt.o"
    if "$AS" "$opt" -o "$TMP/opt.o" "$TMP/nop.s" 2> "$TMP/err"; then
        echo "FAIL: with $opt, it assembles"; exit 1
    fi
    [ ! -e "$TMP/opt.o" ] || { echo "FAIL: with $opt, it writes an object"; exit 1; }
done

echo "ok: build matrix"
