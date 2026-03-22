#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
TMP=${TMPDIR:-/tmp}/as-i8086-realmode-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

SRC="$TMP/realmode.s"
OBJ="$TMP/realmode.o"
BIN="$TMP/realmode.bin"

cat > "$SRC" <<'EOF'
.text
.code16
.arch i8086
.globl realmode
.type realmode,@function
realmode:
    ljmp $0x1234,$0x5678
    lcall $0x4321,$0x1111
    mov %es:4(%bx,%si), %ax
    mov %ax, %ss:6(%bp,%di)
    jne 1f
    nop
1:
    jcxz 2f
    loop 2f
2:  nop
    .org 0x20
after_org:
    nop
.size realmode, .-realmode
EOF

"$AS" --32 -o "$OBJ" "$SRC"
objcopy -O binary --only-section=.text "$OBJ" "$BIN"

hex=$(od -An -tx1 -v "$BIN" | tr -d ' \n')
expect="ea785634129a11112143268b400436894306750190e302e2009000000000000090"

if [ "$hex" != "$expect" ]; then
    echo "unexpected .code16 text bytes" >&2
    echo "got:    $hex" >&2
    echo "expect: $expect" >&2
    exit 1
fi

if ! readelf --wide -s "$OBJ" | grep -Eq '[[:space:]]after_org$'; then
    echo "after_org symbol missing" >&2
    exit 1
fi

after_off=$(readelf --wide -s "$OBJ" | awk '$NF=="after_org"{print $2}')
if [ "$after_off" != "00000020" ]; then
    echo "after_org offset mismatch: $after_off" >&2
    exit 1
fi

echo "ok: i8086 realmode core"
