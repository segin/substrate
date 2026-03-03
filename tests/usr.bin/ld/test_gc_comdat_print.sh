#!/bin/sh
set -eu

# Reqs: LD-E-004, LD-U-010

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-gc-comdat-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/gc_comdat.s" <<'SRC'
.section .text.grp,"axG",@progbits,grp_sig,comdat
.globl _start
.type _start,@function
_start:
	call grp_fn
	ret

.globl grp_fn
.type grp_fn,@function
grp_fn:
	ret

.section .rodata.grp,"aG",@progbits,grp_sig,comdat
.globl grp_const
grp_const:
	.byte 0x7f

.section .text.dead,"ax",@progbits
.globl dead_fn
.type dead_fn,@function
dead_fn:
	ret
SRC

as --64 -o "$TMP/gc_comdat.o" "$TMP/gc_comdat.s"

"$LDX" -m64 -r --gc-sections --print-gc-sections -o "$TMP/out_gc.o" "$TMP/gc_comdat.o" \
	>"$TMP/out_gc.stdout" 2>"$TMP/out_gc.stderr"

readelf -SW "$TMP/out_gc.o" > "$TMP/out_gc.sec"
if grep -q "[[:space:]]\\.text.dead[[:space:]]" "$TMP/out_gc.sec"; then
	echo "FAIL: --gc-sections did not discard dead non-COMDAT section" >&2
	cat "$TMP/out_gc.sec" >&2
	exit 1
fi
if ! grep -q "[[:space:]]\\.rodata.grp[[:space:]]" "$TMP/out_gc.sec"; then
	echo "FAIL: COMDAT-aware GC dropped group peer section unexpectedly" >&2
	cat "$TMP/out_gc.sec" >&2
	exit 1
fi
if ! grep -q "gc-sections: removing .text.dead" "$TMP/out_gc.stderr"; then
	echo "FAIL: --print-gc-sections did not emit discard diagnostics" >&2
	cat "$TMP/out_gc.stderr" >&2
	exit 1
fi

echo "ok: ld COMDAT-aware GC keeps group peers and reports discarded sections"
