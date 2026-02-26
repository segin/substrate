#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
ELFDIR="$TOP/usr.lib/elfobj"
EXDIR="$ELFDIR/examples"
LOCAL_AS="$TOP/usr.bin/as/as"
LOCAL_LD="$TOP/usr.bin/ld/ld"
TMP="$SCRIPT_DIR/.tool_integration"

rm -rf "$TMP"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

if [ ! -x "$LOCAL_AS" ] || [ ! -x "$LOCAL_LD" ]; then
	echo "missing local as/ld wrappers ($LOCAL_AS, $LOCAL_LD)" >&2
	exit 1
fi

make -C "$ELFDIR" >/dev/null
make -C "$EXDIR" clean all >/dev/null
(cd "$EXDIR" && ./create_minimal)
cp "$EXDIR/example_minimal.o" "$TMP/libelfobj_sample.o"

"$LOCAL_LD" -m32 -r -o "$TMP/libelfobj_linked.o" "$TMP/libelfobj_sample.o"

cat > "$TMP/min.s" <<'EOF'
	.text
	.globl _start
_start:
	nop
	ret
EOF

"$LOCAL_AS" -32 -o "$TMP/as_sample.o" "$TMP/min.s"
"$LOCAL_LD" -m32 -r -o "$TMP/as_linked.o" "$TMP/as_sample.o"

if command -v readelf >/dev/null 2>&1; then
	readelf -h "$TMP/libelfobj_linked.o" >/dev/null
fi
if command -v objdump >/dev/null 2>&1; then
	objdump -h "$TMP/libelfobj_linked.o" >/dev/null
fi
if command -v nm >/dev/null 2>&1; then
	nm "$TMP/libelfobj_linked.o" >/dev/null
fi
if command -v strip >/dev/null 2>&1; then
	strip -o "$TMP/libelfobj_stripped.o" "$TMP/libelfobj_linked.o"
	test -f "$TMP/libelfobj_stripped.o"
fi

make -C "$EXDIR" clean >/dev/null
rm -f "$EXDIR"/example_minimal.o "$EXDIR"/example_reloc.o "$EXDIR"/example_merge.o
exit 0
