#!/bin/sh
set -eu

# Reqs: LD-S-001, LD-O-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-plugin-archive-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/start.S" <<'SRC'
	.text
	.globl _start
_start:
	call foo
	xor %rdi, %rdi
	mov $60, %rax
	syscall
SRC
gcc -m64 -c -o "$TMP/start.o" "$TMP/start.S"

cat > "$TMP/ltofoo.S" <<'SRC'
	.text
	.globl foo
foo:
	call __plugin_expected_symbol
	xor %eax, %eax
	ret
	.section .gnu.lto_.foo,"a",@progbits
	.byte 0
SRC
gcc -m64 -c -o "$TMP/ltofoo.o" "$TMP/ltofoo.S"

cat > "$TMP/matfoo.c" <<'SRC'
int foo(void) { return 7; }
SRC
gcc -m64 -c -o "$TMP/matfoo.o" "$TMP/matfoo.c"

ar crsT "$TMP/liblto.a" "$TMP/ltofoo.o"

if "$LDX" -m64 -L"$TMP" -o "$TMP/fail.out" "$TMP/start.o" -llto >"$TMP/no_plugin.log" 2>&1; then
	echo "FAIL: link unexpectedly succeeded without plugin materialization" >&2
	exit 1
fi
if ! grep -Eq "undefined symbol|undefined reference|unresolved" "$TMP/no_plugin.log"; then
	echo "FAIL: missing unresolved diagnostic without plugin" >&2
	sed -n '1,120p' "$TMP/no_plugin.log" >&2
	exit 1
fi

cat > "$TMP/plugin.sh" <<'SRC'
#!/bin/sh
set -eu
case "${1:-}" in
	--version)
		exit 0
		;;
	--materialize)
		echo "$2" >>"${PLUGIN_LOG}"
		case "$2" in
			*ltofoo.o)
				echo "${PLUGIN_MAT}"
				;;
			*)
				echo "$2"
				;;
		esac
		exit 0
		;;
esac
exit 1
SRC
chmod 0755 "$TMP/plugin.sh"
: >"$TMP/plugin.log"
PLUGIN_LOG="$TMP/plugin.log" PLUGIN_MAT="$TMP/matfoo.o" \
	"$LDX" -m64 -L"$TMP" -plugin "$TMP/plugin.sh" -o "$TMP/pass.out" "$TMP/start.o" -llto

if ! grep -q 'ltofoo.o' "$TMP/plugin.log"; then
	echo "FAIL: plugin did not materialize thin archive member" >&2
	sed -n '1,120p' "$TMP/plugin.log" >&2
	exit 1
fi
if [ ! -s "$TMP/pass.out" ]; then
	echo "FAIL: output binary missing after plugin materialization" >&2
	exit 1
fi

echo "ok: plugin materialization integrates with thin-archive extraction loops"
