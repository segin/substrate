#!/bin/sh
set -eu

# Reqs: LD-U-006

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-ifunc-basic-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/ifunc.c" <<'SRC'
static int impl(void) { return 7; }
static void *resolver(void) { return impl; }
int foo(void) __attribute__((ifunc("resolver")));
int main(void) { return foo(); }
SRC

gcc -m64 -O2 -fPIC -c -o "$TMP/ifunc.o" "$TMP/ifunc.c"

if ! readelf -Ws "$TMP/ifunc.o" | grep -q " foo$"; then
	echo "FAIL: IFUNC fixture did not produce foo symbol" >&2
	readelf -Ws "$TMP/ifunc.o" >&2 || true
	exit 1
fi

"$LDX" -m64 -static -e main -o "$TMP/ifunc.out" "$TMP/ifunc.o"

if ! readelf -Ws "$TMP/ifunc.out" | awk '$NF=="foo"{print}' | grep -Eq "IFUNC|<OS specific>: 10"; then
	echo "FAIL: IFUNC symbol type for foo not preserved in output" >&2
	readelf -Ws "$TMP/ifunc.out" >&2 || true
	exit 1
fi

echo "ok: x86-64 IFUNC symbol links and is preserved"
