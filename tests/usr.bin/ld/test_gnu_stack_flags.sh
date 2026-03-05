#!/bin/sh
set -eu

# Reqs: LD-U-009, LD-W-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-zstack-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/main.c" <<'SRC'
int _start(void) { return 0; }
SRC
gcc -m64 -c -o "$TMP/main.o" "$TMP/main.c"

"$LDX" -m64 -o "$TMP/default.out" "$TMP/main.o"
"$LDX" -m64 -z execstack -o "$TMP/execstack.out" "$TMP/main.o"
"$LDX" -m64 -z noexecstack -o "$TMP/noexecstack.out" "$TMP/main.o"

readelf -Wl "$TMP/default.out" > "$TMP/default.ph"
readelf -Wl "$TMP/execstack.out" > "$TMP/execstack.ph"
readelf -Wl "$TMP/noexecstack.out" > "$TMP/noexecstack.ph"

if ! grep -q " GNU_STACK " "$TMP/default.ph"; then
	echo "FAIL: default output missing PT_GNU_STACK" >&2
	cat "$TMP/default.ph" >&2
	exit 1
fi
if ! grep -q " GNU_STACK " "$TMP/execstack.ph"; then
	echo "FAIL: -z execstack output missing PT_GNU_STACK" >&2
	cat "$TMP/execstack.ph" >&2
	exit 1
fi
if ! grep -q " GNU_STACK " "$TMP/noexecstack.ph"; then
	echo "FAIL: -z noexecstack output missing PT_GNU_STACK" >&2
	cat "$TMP/noexecstack.ph" >&2
	exit 1
fi

if ! awk '/GNU_STACK/ { if ($0 ~ /E/) ok=1 } END { exit ok ? 0 : 1 }' "$TMP/execstack.ph"; then
	echo "FAIL: -z execstack did not set executable PT_GNU_STACK" >&2
	cat "$TMP/execstack.ph" >&2
	exit 1
fi
if awk '/GNU_STACK/ { if ($0 ~ /E/) bad=1 } END { exit bad ? 0 : 1 }' "$TMP/noexecstack.ph"; then
	echo "FAIL: -z noexecstack unexpectedly set executable PT_GNU_STACK" >&2
	cat "$TMP/noexecstack.ph" >&2
	exit 1
fi

echo "ok: ld maps -z execstack/noexecstack to PT_GNU_STACK flags"
