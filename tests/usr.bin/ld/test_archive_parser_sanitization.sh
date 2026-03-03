#!/bin/sh
set -eu

# Reqs: LD-R-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
CORPUS_DIR="$ROOT/tests/usr.bin/ld/corpus/archive"
TMP=${TMPDIR:-/tmp}/ldx86-archive-fuzz-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

for sample in "$CORPUS_DIR"/*.a; do
	if "$LDX" -m64 -r -o "$TMP/out_sample.o" "$sample" >"$TMP/sample.err" 2>&1; then
		echo "FAIL: malformed archive corpus sample unexpectedly linked: $sample" >&2
		exit 1
	fi
done

for i in $(seq 1 40); do
	path="$TMP/fuzz_$i.a"
	case $((i % 3)) in
	0)
		printf '!<arch>\n' > "$path"
		dd if=/dev/urandom bs=1 count=$((32 + i)) status=none >> "$path"
		;;
	1)
		printf '!<thin>\n' > "$path"
		dd if=/dev/urandom bs=1 count=$((32 + i)) status=none >> "$path"
		;;
	2)
		dd if=/dev/urandom bs=1 count=$((64 + i)) status=none > "$path"
		;;
	esac
	if "$LDX" -m64 -r -o "$TMP/out_fuzz_$i.o" "$path" >"$TMP/fuzz_$i.err" 2>&1; then
		echo "FAIL: fuzz archive unexpectedly linked: $path" >&2
		exit 1
	fi
done

echo "ok: ld archive parser sanitization corpus and fuzz inputs"
