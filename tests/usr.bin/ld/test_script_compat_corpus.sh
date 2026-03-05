#!/bin/sh
set -eu

# Reqs: LD-U-007 LD-S-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
CORPUS_DIR="$ROOT/tests/usr.bin/ld/corpus/scripts"
TMP=${TMPDIR:-/tmp}/ldx86-script-corpus-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/base.c" <<'SRC'
__attribute__((section(".text.keep"))) void keep_fn(void) {}
__attribute__((section(".text.drop"))) void drop_fn(void) {}
__attribute__((section(".discard_me"))) int doomed = 7;
int g_data = 1;
int _start(void) { return g_data; }
SRC
gcc -m64 -ffunction-sections -fdata-sections -fno-pie -ffreestanding -c -o "$TMP/base.o" "$TMP/base.c"

cat > "$TMP/phdr.c" <<'SRC'
int g_data = 1;
int _start(void) { return g_data; }
SRC
gcc -m64 -fno-pie -ffreestanding -c -o "$TMP/phdr.o" "$TMP/phdr.c"

for script in "$CORPUS_DIR"/*.ld; do
	name=$(basename "$script")
	out="$TMP/${name%.ld}.out"
	log="$TMP/${name%.ld}.log"
	expect=pass
	case "$name" in
		002_assert_fail.ld) expect=fail ;;
	esac

	case "$name" in
		006_phdrs.ld)
			cmd="$LDX -m64 --gc-sections -T $script -o $out $TMP/phdr.o"
			;;
		*)
			cmd="$LDX -m64 -r --gc-sections -T $script -o $out $TMP/base.o"
			;;
	esac

	if sh -c "$cmd" >"$log" 2>&1; then
		rc=0
	else
		rc=1
	fi

	if [ "$expect" = "pass" ] && [ "$rc" -ne 0 ]; then
		echo "FAIL: corpus case $name expected pass" >&2
		sed -n '1,80p' "$log" >&2
		exit 1
	fi
	if [ "$expect" = "fail" ] && [ "$rc" -eq 0 ]; then
		echo "FAIL: corpus case $name expected fail" >&2
		exit 1
	fi
done

echo "ok: linker-script compatibility corpus cases pass"
