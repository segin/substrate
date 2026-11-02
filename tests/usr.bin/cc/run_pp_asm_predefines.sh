#!/bin/sh
# A .S file is preprocessed with gcc's predefined macros: __ASSEMBLER__,
# wherever the output goes, and no __ASSEMBLY__, which is Linux's own
# -D.  cc defined both, and neither when -E wrote to standard output.
# Each is tested with -E to a file, -E to a pipe and -c, and is what gcc
# makes of it; a -D__ASSEMBLY__ is still honoured.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_asm_predefines: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
export AS

cat >"$CC_TMP/p.S" <<'EOF'
	.data
#ifdef __ASSEMBLER__
	.byte 1
#else
	.byte 2
#endif
#ifdef __ASSEMBLY__
	.byte 3
#else
	.byte 4
#endif
EOF
norm() { grep -v '^#' | sed 's/^[ \t]*//' | grep -v '^$'; }
for d in "" -D__ASSEMBLY__; do
	gcc $d -E -P "$CC_TMP/p.S" | norm >"$CC_TMP/g.out"
	"$CC_BIN" $d -E -P -o "$CC_TMP/f.raw" "$CC_TMP/p.S"
	norm <"$CC_TMP/f.raw" >"$CC_TMP/f.out"
	"$CC_BIN" $d -E -P "$CC_TMP/p.S" | norm >"$CC_TMP/p.out"
	if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/f.out" || ! cmp -s "$CC_TMP/g.out" "$CC_TMP/p.out"; then
		echo "run_pp_asm_predefines: $d -E gives [$(tr '\n' ' ' <"$CC_TMP/f.out")] to a file and" \
			"[$(tr '\n' ' ' <"$CC_TMP/p.out")] to a pipe, gcc's [$(tr '\n' ' ' <"$CC_TMP/g.out")]"
		exit 1
	fi
	gcc $d -c -o "$CC_TMP/g.o" "$CC_TMP/p.S"
	"$CC_BIN" $d -c -o "$CC_TMP/c.o" "$CC_TMP/p.S"
	objcopy -O binary -j .data "$CC_TMP/g.o" "$CC_TMP/g.bin"
	objcopy -O binary -j .data "$CC_TMP/c.o" "$CC_TMP/c.bin"
	if ! cmp -s "$CC_TMP/g.bin" "$CC_TMP/c.bin"; then
		echo "run_pp_asm_predefines: $d -c assembles $(od -An -tx1 "$CC_TMP/c.bin"), gcc's $(od -An -tx1 "$CC_TMP/g.bin")"
		exit 1
	fi
done
echo "run_pp_asm_predefines: ok"
