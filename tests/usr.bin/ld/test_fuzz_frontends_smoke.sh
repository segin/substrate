#!/bin/sh
set -eu

# Reqs: LD-R-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-fuzz-smoke-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/base.c" <<'SRC'
int base(void) { return 0; }
SRC
gcc -m64 -c -o "$TMP/base.o" "$TMP/base.c"

n=1
while [ "$n" -le 40 ]; do
	raw="$TMP/raw_$n.bin"
	dd if=/dev/urandom of="$raw" bs=1 count=256 status=none
	set +e
	"$LDX" -m64 -r -o "$TMP/out_$n.o" "$raw" >/dev/null 2>&1
	rc=$?
	set -e
	if [ "$rc" -ge 128 ]; then
		echo "FAIL: ld crashed on random object-like input (rc=$rc, case=$n)" >&2
		exit 1
	fi
	n=$((n + 1))
done

n=1
while [ "$n" -le 40 ]; do
	scr="$TMP/script_$n.ld"
	dd if=/dev/urandom of="$scr" bs=1 count=256 status=none
	set +e
	"$LDX" -m64 -r -T "$scr" -o "$TMP/sout_$n.o" "$TMP/base.o" >/dev/null 2>&1
	rc=$?
	set -e
	if [ "$rc" -ge 128 ]; then
		echo "FAIL: ld crashed on random script input (rc=$rc, case=$n)" >&2
		exit 1
	fi
	n=$((n + 1))
done

echo "ok: ELF/archive/script frontends survive fixed-budget fuzz smoke inputs"
