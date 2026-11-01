#!/bin/sh
# -fpatchable-function-entry=N,M puts N nops at each function's entry,
# M of them before its label and the rest after it (after an endbr,
# before -pg's call), and records the first's address in
# __patchable_function_entries, as gcc does; Linux's call padding is 16
# before each function.  cc took the option and ignored it.  Each
# function of the object has its nops where gcc puts them, the section
# has an entry at each function's first nop, and the program runs as
# gcc's; a bad N,M is refused.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objcopy >/dev/null 2>&1 ||
	! command -v readelf >/dev/null 2>&1 || ! command -v od >/dev/null 2>&1; then
	echo "run_patchable_function_entry: skip (no gcc, objcopy, readelf or od)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

__attribute__((noinline)) long leaf(long x) { return x * 3 + 1; }
__attribute__((noinline)) long walk(int n)
{
	long t = 0;
	int i;

	for (i = 0; i < n; ++i) {
		t += leaf(i);
	}
	return t;
}

int main(void)
{
	printf("%ld\n", walk(10));
	return 0;
}
EOF2
# the byte at offset $2 of .text in $1, as two hex digits
text_byte() {
	od -An -tx1 -j "$2" -N1 "$1" | tr -d ' \n'
}
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/b.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	if [ $m = 64 ]; then ptr=8; else ptr=4; fi
	for nm in 16,16 3,1 2,0 5,5; do
		n=${nm%,*}
		before=${nm#*,}
		for extra in "" "-fcf-protection=branch -pg -mfentry"; do
			for o in O0 O2; do
				for path in mir legacy; do
					run="env"
					if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
					what="-m$m -$o $path =$nm [$extra]"
					# shellcheck disable=SC2086
					if ! $run "$CC_BIN" -m$m -$o -fpatchable-function-entry=$nm $extra \
						-c -o "$CC_TMP/b.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
						echo "run_patchable_function_entry: $what: $(head -1 "$CC_TMP/c.err")"
						exit 1
					fi
					objcopy -O binary -j .text "$CC_TMP/b.o" "$CC_TMP/text.bin"
					skip=0
					if [ -n "$extra" ]; then skip=4; fi # (the endbr)
					entries=""
					for fn in leaf walk main; do
						at=$((0x$(readelf -sW "$CC_TMP/b.o" | awk -v f="$fn" '$8 == f { print $2 }')))
						i=$((at - before))
						while [ $i -lt $at ]; do
							if [ "$(text_byte "$CC_TMP/text.bin" $i)" != 90 ]; then
								echo "run_patchable_function_entry: $what: $fn: no nop at -$((at - i))"
								exit 1
							fi
							i=$((i + 1))
						done
						i=$((at + skip))
						while [ $i -lt $((at + skip + n - before)) ]; do
							if [ "$(text_byte "$CC_TMP/text.bin" $i)" != 90 ]; then
								echo "run_patchable_function_entry: $what: $fn: no nop at +$((i - at))"
								exit 1
							fi
							i=$((i + 1))
						done
						# not one more than N
						if [ "$(text_byte "$CC_TMP/text.bin" $i)" = 90 ]; then
							echo "run_patchable_function_entry: $what: $fn: more than $n nops"
							exit 1
						fi
						if [ "$before" -gt 0 ]; then first=$((at - before)); else first=$((at + skip)); fi
						entries="$entries $first"
					done
					# the section's entries: each function's first nop, by .text + its offset
					got=""
					for a in $(readelf -rW "$CC_TMP/b.o" | awk '
						/^Relocation section/ { on = $3 ~ /__patchable_function_entries/; next }
						on && $5 == ".text" { print $7 }'); do
						got="$got $((0x$a))"
					done
					got=$(echo "$got" | tr ' ' '\n' | grep . | sort -n | tr '\n' ' ')
					if [ $m = 32 ]; then
						# (REL: the addend is the word in the section)
						objcopy -O binary -j __patchable_function_entries "$CC_TMP/b.o" "$CC_TMP/pfe.bin"
						got=$(od -An -tu4 -v "$CC_TMP/pfe.bin" | tr -s ' \n' '\n\n' | grep . | sort -n | tr '\n' ' ')
					fi
					exp=$(echo "$entries" | tr ' ' '\n' | grep . | sort -n | tr '\n' ' ')
					if [ "$got" != "$exp" ]; then
						echo "run_patchable_function_entry: $what: entries [$got], not [$exp]"
						exit 1
					fi
					size=$((0x$(readelf -SW "$CC_TMP/b.o" | awk '{ for (i = 1; i < NF; ++i) if ($i == "__patchable_function_entries") print $(i + 4) }')))
					if [ "$size" != $((3 * ptr)) ]; then
						echo "run_patchable_function_entry: $what: the section is $size bytes, not $((3 * ptr))"
						exit 1
					fi
					if [ -z "$extra" ]; then
						gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/b.o"
						if [ "$("$CC_TMP/c")" != "$want" ]; then
							echo "run_patchable_function_entry: $what prints [$("$CC_TMP/c")], gcc's [$want]"
							exit 1
						fi
					fi
				done
			done
		done
	done
done
for v in 16,17 x 3, -1; do
	if "$CC_BIN" -fpatchable-function-entry=$v -c -o "$CC_TMP/n.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
		echo "run_patchable_function_entry: -fpatchable-function-entry=$v was taken"
		exit 1
	fi
done
echo "run_patchable_function_entry: ok"
