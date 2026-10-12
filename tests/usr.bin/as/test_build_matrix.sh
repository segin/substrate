#!/bin/sh
# The names the assembler is installed under.
#
# The Makefile makes four links to it beside it and in $(BINDIR): as.x86
# and as.x64, and arm-as and aarch64-as, and the assembler must run
# under each and write that machine's object.  The last two were made
# when it could assemble for neither -- run as aarch64-as it wrote `ret`
# as the byte C3 into an x86-64 object and exited 0 -- and were then
# taken out, and refused; they are back with the encoders
# (test_arm_targets.sh).
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
for link in as.x86 as.x64 arm-as aarch64-as; do
    # shellcheck disable=SC2016
    grep -q "ln -sf \$(PROG) $link\$" "$mk" ||
        { echo "FAIL: the Makefile does not link $link beside the assembler"; exit 1; }
    # shellcheck disable=SC2016
    grep -q "ln -sf as \$(BINDIR)/$link\$" "$mk" ||
        { echo "FAIL: the Makefile does not install $link"; exit 1; }
done

printf '\t.text\n\tnop\n' > "$TMP/nop.s"
for link in as.x86 as.x64; do
    ln -s "$AS" "$TMP/$link"
    "$TMP/$link" -32 -o "$TMP/$link.o" "$TMP/nop.s" ||
        { echo "FAIL: the assembler does not run as $link"; exit 1; }
    [ "$(objcopy -O binary --only-section=.text "$TMP/$link.o" /dev/stdout | od -An -tx1 | tr -d ' \n')" = 90 ] ||
        { echo "FAIL: as $link, nop is not the byte 90"; exit 1; }
done

# (The rest is of this assembler's own names and options: GNU as for
# x86 has neither.)
if "$AS" --version 2>/dev/null | grep -qi 'GNU assembler'; then
    echo "ok: build matrix (the x86 names; the others are this assembler's own)"
    exit 0
fi
for pair in arm-as:ARM aarch64-as:AArch64; do
    link=${pair%%:*}
    ln -s "$AS" "$TMP/$link"
    "$TMP/$link" -o "$TMP/$link.o" "$TMP/nop.s" ||
        { echo "FAIL: the assembler does not run as $link"; exit 1; }
    [ "$(readelf -h "$TMP/$link.o" | sed -n 's/^ *Machine: *//p')" = "${pair#*:}" ] ||
        { echo "FAIL: as $link, the object is not ${pair#*:}'s"; exit 1; }
done

# Asked for a machine it has not, or for ARM's options with no ARM
# target, it fails and writes no object.
for opt in --target=sparc -marm -mthumb -mcpu=cortex-a53 -mfpu=neon; do
    rm -f "$TMP/opt.o"
    if "$AS" "$opt" -o "$TMP/opt.o" "$TMP/nop.s" 2> "$TMP/err"; then
        echo "FAIL: with $opt, it assembles"; exit 1
    fi
    [ ! -e "$TMP/opt.o" ] || { echo "FAIL: with $opt, it writes an object"; exit 1; }
done
ln -s "$AS" "$TMP/thumb-as"
if "$TMP/thumb-as" -o "$TMP/thumb.o" "$TMP/nop.s" 2> "$TMP/err"; then
    echo "FAIL: run as thumb-as, it assembles"; exit 1
fi

echo "ok: build matrix"
