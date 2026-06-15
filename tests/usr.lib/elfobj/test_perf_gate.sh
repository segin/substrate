#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
BENCH_DIR="$TOP/usr.lib/elfobj/bench"
OUT="$SCRIPT_DIR/.perf_bench.out"

MAX_WRITE=${ELFOBJ_MAX_WRITE_10K_MS:-2000}
MAX_LINK=${ELFOBJ_MAX_LINK_LARGE_MS:-4000}
MAX_READ=${ELFOBJ_MAX_READ_KERNEL_MS:-5000}

make -C "$BENCH_DIR" >/dev/null
"$BENCH_DIR/bench_elf" "${ELFOBJ_BENCH_IMAGE:-}" > "$OUT"

write_ms=$(awk -F= '/write_10k_symbols_ms=/{print $2}' "$OUT")
link_ms=$(awk -F= '/link_large_archive_ms=/{print $2}' "$OUT")
read_ms=$(awk -F= '/read_kernel_image_ms=/{print $2}' "$OUT")

fail=0

if [ -z "$write_ms" ] || [ -z "$link_ms" ]; then
	echo "perf gate: missing benchmark metrics" >&2
	rm -f "$OUT"
	exit 1
fi

awk "BEGIN { exit !($write_ms <= $MAX_WRITE) }" || {
	echo "perf gate: write_10k_symbols_ms=$write_ms exceeds $MAX_WRITE" >&2
	fail=1
}
awk "BEGIN { exit !($link_ms <= $MAX_LINK) }" || {
	echo "perf gate: link_large_archive_ms=$link_ms exceeds $MAX_LINK" >&2
	fail=1
}

if [ "$read_ms" != "SKIP" ]; then
	awk "BEGIN { exit !($read_ms <= $MAX_READ) }" || {
		echo "perf gate: read_kernel_image_ms=$read_ms exceeds $MAX_READ" >&2
		fail=1
	}
fi

rm -f "$OUT"
if [ "$fail" -ne 0 ]; then
	exit 1
fi
exit 0
