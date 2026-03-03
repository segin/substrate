#!/bin/sh
set -eu

# Reqs: LD-U-007, LD-E-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-gc-matrix-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/root.c" <<'SRC'
int used_fn(void);
extern int used_data;
int _start(void) { return used_fn() + used_data; }
SRC

cat > "$TMP/extras.c" <<'SRC'
int used_fn(void) { return 1; }
int used_data = 2;
int unused_fn(void) { return 3; }
int unused_data = 4;
__attribute__((constructor, used)) void ctor_fn(void) { used_data++; }
__attribute__((weak)) int weak_unused(void) { return 7; }
SRC

gcc -m64 -ffunction-sections -fdata-sections -c -o "$TMP/root.o" "$TMP/root.c"
gcc -m64 -ffunction-sections -fdata-sections -c -o "$TMP/extras.o" "$TMP/extras.c"

"$LDX" -m64 -r --gc-sections --print-gc-sections -o "$TMP/out_gc.o" "$TMP/root.o" "$TMP/extras.o" \
	>"$TMP/out_gc.stdout" 2>"$TMP/out_gc.stderr"
readelf -SW "$TMP/out_gc.o" > "$TMP/out_gc.sec"

for keep_pat in \
	"[[:space:]]\\.text\\._start[[:space:]]" \
	"[[:space:]]\\.text\\.used_fn[[:space:]]" \
	"[[:space:]]\\.data\\.used_data[[:space:]]" \
	"[[:space:]]\\.init_array[[:space:]]" \
	"[[:space:]]\\.text\\.ctor_fn[[:space:]]"
do
	if ! grep -E "$keep_pat" "$TMP/out_gc.sec" >/dev/null; then
		echo "FAIL: missing expected live section ($keep_pat) after --gc-sections" >&2
		cat "$TMP/out_gc.sec" >&2
		exit 1
	fi
done

for drop_pat in \
	"[[:space:]]\\.text\\.unused_fn[[:space:]]" \
	"[[:space:]]\\.data\\.unused_data[[:space:]]" \
	"[[:space:]]\\.text\\.weak_unused[[:space:]]"
do
	if grep -E "$drop_pat" "$TMP/out_gc.sec" >/dev/null; then
		echo "FAIL: found dead section that should have been gc-collected ($drop_pat)" >&2
		cat "$TMP/out_gc.sec" >&2
		exit 1
	fi
done

if ! grep -q "gc-sections: removing" "$TMP/out_gc.stderr"; then
	echo "FAIL: expected --print-gc-sections diagnostics were not emitted" >&2
	cat "$TMP/out_gc.stderr" >&2
	exit 1
fi

echo "ok: ld gc matrix covers function/data, ctors, weak refs, and diagnostics"
