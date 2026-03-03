#!/bin/sh
set -eu

# Reqs: LD-S-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-orphan-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/orphan.s" <<'SRC'
.section .sec_rw,"aw",@progbits
.byte 0x11

.section .sec_exec,"ax",@progbits
.globl sec_exec_fn
.type sec_exec_fn,@function
sec_exec_fn:
	ret

.section .sec_ro,"a",@progbits
.byte 0x22

.section .sec_bss,"aw",@nobits
.space 8
SRC

as --64 -o "$TMP/orphan.o" "$TMP/orphan.s"
"$LDX" -m64 -r -o "$TMP/out.o" "$TMP/orphan.o"
readelf -SW "$TMP/out.o" > "$TMP/out.sec"

exec_ln=$(awk '/[[:space:]]\.sec_exec[[:space:]]/ {print NR; exit}' "$TMP/out.sec")
ro_ln=$(awk '/[[:space:]]\.sec_ro[[:space:]]/ {print NR; exit}' "$TMP/out.sec")
rw_ln=$(awk '/[[:space:]]\.sec_rw[[:space:]]/ {print NR; exit}' "$TMP/out.sec")
bss_ln=$(awk '/[[:space:]]\.sec_bss[[:space:]]/ {print NR; exit}' "$TMP/out.sec")

if [ -z "$exec_ln" ] || [ -z "$ro_ln" ] || [ -z "$rw_ln" ] || [ -z "$bss_ln" ]; then
	echo "FAIL: expected orphan test sections missing in linked output" >&2
	cat "$TMP/out.sec" >&2
	exit 1
fi
if [ "$exec_ln" -ge "$ro_ln" ] || [ "$ro_ln" -ge "$rw_ln" ] || [ "$rw_ln" -ge "$bss_ln" ]; then
	echo "FAIL: orphan section placement order is not exec->ro->rw->bss" >&2
	cat "$TMP/out.sec" >&2
	exit 1
fi

echo "ok: ld applies deterministic orphan placement heuristics by section class"
