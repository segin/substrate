#!/bin/sh
set -eu

# Reqs: LD-U-009, LD-U-010

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-phdr-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/dep.c" <<'SRC'
int dep(void) { return 7; }
SRC
gcc -m64 -fPIC -c -o "$TMP/dep.o" "$TMP/dep.c"
gcc -shared -o "$TMP/libdep.so" "$TMP/dep.o"

cat > "$TMP/main.c" <<'SRC'
int dep(void);
__thread int tls_v;
int _start(void) { return dep() + tls_v; }
SRC
gcc -m64 -fPIC -c -o "$TMP/main.o" "$TMP/main.c"

cat > "$TMP/meta.S" <<'SRC'
	.text
	.globl phdr_meta_anchor
phdr_meta_anchor:
	ret

	.section .note.test,"a",@note
	.align 4
	.long 4
	.long 4
	.long 1
	.asciz "TEST"
	.align 4
	.long 0

	.section .note.gnu.property,"a",@note
	.align 8
	.long 4
	.long 8
	.long 5
	.asciz "GNU"
	.align 8
	.long 0
	.long 0

	.section .eh_frame_hdr,"a",@progbits
	.byte 1, 0, 0, 0
SRC
gcc -m64 -c -o "$TMP/meta.o" "$TMP/meta.S"

"$LDX" -m64 -shared -dynamic-linker /lib64/ld-linux-x86-64.so.2 \
	-L"$TMP" -ldep -o "$TMP/out.so" "$TMP/main.o" "$TMP/meta.o"

readelf -Wl "$TMP/out.so" > "$TMP/phdr.txt"

for ph in PHDR LOAD INTERP DYNAMIC TLS NOTE GNU_STACK GNU_RELRO GNU_EH_FRAME GNU_PROPERTY; do
	if ! grep -q " $ph " "$TMP/phdr.txt"; then
		echo "FAIL: missing program header $ph" >&2
		cat "$TMP/phdr.txt" >&2
		exit 1
	fi
done

if awk '/^[[:space:]]*LOAD[[:space:]]/ { if ($0 ~ /W E|E W|RWE|RWX/) bad=1 } END { exit bad ? 0 : 1 }' "$TMP/phdr.txt"; then
	echo "FAIL: found writable+executable PT_LOAD segment" >&2
	cat "$TMP/phdr.txt" >&2
	exit 1
fi

echo "ok: ld emits expected PHDR set and preserves W^X PT_LOAD policy"
