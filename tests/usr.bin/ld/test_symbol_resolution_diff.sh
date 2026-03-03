#!/bin/sh
set -eu

# Reqs: LD-U-007, LD-E-003

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
GNU_LD=${GNU_LD:-$(command -v ld || true)}
LLD=${LLD:-$(command -v ld.lld || true)}
TMP=${TMPDIR:-/tmp}/ldx86-sym-diff-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

if [ -z "$GNU_LD" ]; then
	echo "FAIL: GNU ld not found in PATH" >&2
	exit 1
fi
if [ -z "$LLD" ]; then
	echo "FAIL: ld.lld not found in PATH" >&2
	exit 1
fi

sym_sig() {
	readelf -Ws "$1" | awk -v name="$2" '
		BEGIN { found = 0; }
		$NF == name && $4 != "FILE" {
			printf("%s|%s|%s|%s\n", $4, $5, $7, $3);
			found = 1;
			exit(0);
		}
		END {
			if (!found)
				exit(1);
		}
	'
}

compare_sig() {
	file_our="$1"
	file_gnu="$2"
	file_lld="$3"
	sym="$4"
	label="$5"

	our=$(sym_sig "$file_our" "$sym") || {
		echo "FAIL: missing symbol '$sym' in our output ($label)" >&2
		readelf -Ws "$file_our" >&2 || true
		exit 1
	}
	gnu=$(sym_sig "$file_gnu" "$sym") || {
		echo "FAIL: missing symbol '$sym' in GNU ld output ($label)" >&2
		readelf -Ws "$file_gnu" >&2 || true
		exit 1
	}
	lld=$(sym_sig "$file_lld" "$sym") || {
		echo "FAIL: missing symbol '$sym' in lld output ($label)" >&2
		readelf -Ws "$file_lld" >&2 || true
		exit 1
	}
	if [ "$our" != "$gnu" ] || [ "$our" != "$lld" ]; then
		echo "FAIL: symbol signature mismatch for '$sym' ($label)" >&2
		echo "  our: $our" >&2
		echo "  gnu: $gnu" >&2
		echo "  lld: $lld" >&2
		exit 1
	fi
}

cat > "$TMP/common_i32.c" <<'SRC'
int sym;
SRC
cat > "$TMP/common_i64.c" <<'SRC'
long sym;
SRC
cat > "$TMP/weak_def_i32.c" <<'SRC'
__attribute__((weak)) int sym = 42;
SRC
cat > "$TMP/weak_bss_i32.c" <<'SRC'
__attribute__((weak)) int sym;
SRC

gcc -m64 -fcommon -c -o "$TMP/common_i32.o" "$TMP/common_i32.c"
gcc -m64 -fcommon -c -o "$TMP/common_i64.o" "$TMP/common_i64.c"
gcc -m64 -fcommon -c -o "$TMP/weak_def_i32.o" "$TMP/weak_def_i32.c"
gcc -m64 -fcommon -c -o "$TMP/weak_bss_i32.o" "$TMP/weak_bss_i32.c"

# Case 1: common + weak definition
"$LDX" -m64 -r -o "$TMP/our_case1.o" "$TMP/common_i32.o" "$TMP/weak_def_i32.o"
"$GNU_LD" -m elf_x86_64 -r -o "$TMP/gnu_case1.o" "$TMP/common_i32.o" "$TMP/weak_def_i32.o"
"$LLD" -m elf_x86_64 -r -o "$TMP/lld_case1.o" "$TMP/common_i32.o" "$TMP/weak_def_i32.o"
compare_sig "$TMP/our_case1.o" "$TMP/gnu_case1.o" "$TMP/lld_case1.o" sym "common+weak-def"

# Case 2: larger common + weak BSS definition
"$LDX" -m64 -r -o "$TMP/our_case2.o" "$TMP/common_i64.o" "$TMP/weak_bss_i32.o"
"$GNU_LD" -m elf_x86_64 -r -o "$TMP/gnu_case2.o" "$TMP/common_i64.o" "$TMP/weak_bss_i32.o"
"$LLD" -m elf_x86_64 -r -o "$TMP/lld_case2.o" "$TMP/common_i64.o" "$TMP/weak_bss_i32.o"
compare_sig "$TMP/our_case2.o" "$TMP/gnu_case2.o" "$TMP/lld_case2.o" sym "common64+weak-bss32"

echo "ok: weak/common symbol resolution matches GNU ld and lld signatures"
