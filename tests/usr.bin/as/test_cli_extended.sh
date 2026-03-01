#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-cli-extended-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

cat > "$TMP/att.s" <<'SRC'
.text
.globl _start
_start:
mov $1, %eax
ret
SRC

cat > "$TMP/intel.s" <<'SRC'
.intel_syntax noprefix
.text
.globl _start
_start:
mov eax, 1
ret
SRC

"$AS" --target-help > "$TMP/help.txt"
grep -q "x86/i386" "$TMP/help.txt"

"$AS" --statistics -o "$TMP/stats.o" "$TMP/att.s" 2> "$TMP/stats.txt"
[ -s "$TMP/stats.o" ]
grep -q "statistics" "$TMP/stats.txt"

"$AS" -al="$TMP/out.lst" -o "$TMP/listing.o" "$TMP/att.s"
[ -s "$TMP/out.lst" ]
grep -q "_start:" "$TMP/out.lst"

"$AS" -c --no-warn --fatal-warnings --defsym FOO=1 -msyntax=att -o "$TMP/flags.o" "$TMP/att.s"
[ -s "$TMP/flags.o" ]

"$AS" -msyntax=intel -W -o "$TMP/intel.o" "$TMP/intel.s"
[ -s "$TMP/intel.o" ]

echo "ok: as extended CLI"
