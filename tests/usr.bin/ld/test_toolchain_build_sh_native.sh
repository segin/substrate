#!/bin/sh
set -eu

# Reqs: LD-U-001

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CCX="$ROOT/usr.bin/cc/cc"
ASX="$ROOT/usr.bin/as/as"
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-build-sh-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"; make -C "$ROOT/bin/sh" clean >/dev/null 2>&1 || true' EXIT INT TERM

make -C "$ROOT/usr.bin/cc" NATIVE_BUILD=1 >/dev/null
make -C "$ROOT/usr.bin/as" NATIVE_BUILD=1 >/dev/null
make -C "$ROOT/usr.bin/ld" NATIVE_BUILD=1 >/dev/null

make -C "$ROOT/bin/sh" clean >/dev/null
make -C "$ROOT/bin/sh" NATIVE_BUILD=1 \
	CC="$CCX" AS="$ASX" LD="$LDX" CFLAGS='-O2 -Wall -Wextra -Werror -std=gnu99' \
	-j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)" >"$TMP/build.log" 2>&1

if [ ! -x "$ROOT/bin/sh/sh" ]; then
	echo "FAIL: bin/sh build did not produce executable" >&2
	sed -n '1,160p' "$TMP/build.log" >&2
	exit 1
fi
if ! file "$ROOT/bin/sh/sh" | grep -q 'ELF'; then
	echo "FAIL: bin/sh output is not ELF" >&2
	file "$ROOT/bin/sh/sh" >&2 || true
	exit 1
fi

echo "ok: bin/sh builds with internal cc+as+ld on native host path"
