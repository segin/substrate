#!/bin/sh
# An extern declared in a block may have type void, as at file scope: it
# names an object defined elsewhere, such as a linker script's
# __start_notes, and only its address is taken.  Linux's
# init_vmlinux_build_id() declares its notes' bounds so, and cc refused
# them in a block (lib/buildid.c, CONFIG_VMCORE_INFO).  A void that is
# not extern is still an error.  Results are gcc's, -m64 and -m32.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_extern_void_local: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/v.c" <<'EOF2'
#include <stdio.h>

__asm__(".data\n.globl note_start\nnote_start:\n.ascii \"abcdefg\"\n.globl note_stop\nnote_stop:\n.text");

__attribute__((noinline)) static unsigned long span(void)
{
	extern const void note_start;
	extern const void note_stop;

	return (unsigned long)((const char *)&note_stop - (const char *)&note_start);
}

int main(void)
{
	extern const void note_start;

	printf("%lu %c\n", span(), *((const char *)&note_start + 2));
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/v.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		if ! "$CC_BIN" -m$m -$o -c -o "$CC_TMP/v.o" "$CC_TMP/v.c" 2>"$CC_TMP/c.err"; then
			echo "run_extern_void_local: -m$m -$o: $(head -1 "$CC_TMP/c.err")"
			exit 1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/v.o"
		got=$("$CC_TMP/c")
		if [ "$got" != "$want" ]; then
			echo "run_extern_void_local: -m$m -$o prints [$got], gcc's [$want]"
			exit 1
		fi
	done
done
# a local void that is not extern: an error in both
printf 'int f(void)\n{\n\tvoid y;\n\treturn 0;\n}\n' >"$CC_TMP/n.c"
if gcc -c -o "$CC_TMP/n.o" "$CC_TMP/n.c" 2>/dev/null; then
	echo "run_extern_void_local: gcc accepts a local void"
	exit 1
fi
if "$CC_BIN" -c -o "$CC_TMP/n.o" "$CC_TMP/n.c" 2>"$CC_TMP/n.err"; then
	echo "run_extern_void_local: cc accepts a local void"
	exit 1
fi
if ! grep -q 'void variable' "$CC_TMP/n.err"; then
	echo "run_extern_void_local: a local void: $(head -1 "$CC_TMP/n.err")"
	exit 1
fi
echo "run_extern_void_local: ok"
