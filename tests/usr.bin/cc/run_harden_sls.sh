#!/bin/sh
# -mharden-sls=all puts an int3 after each return and each indirect jump,
# where the processor may run on straight past them (Linux's
# CONFIG_MITIGATION_SLS, which objtool checks).  cc took the option and
# ignored it.  Each ret and indirect jmp of the assembly -- a switch's
# jump table, the table's entry through a thunk under
# -mindirect-branch=thunk-extern -- has an int3 after it under =all;
# =return only the rets, =indirect-jmp only the jumps, =none and no
# option neither; a jump to __x86_return_thunk has none, as gcc's has
# not; and the program prints what gcc's does.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_harden_sls: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

struct big { int a[6]; };

__attribute__((noinline)) struct big mk(int x)
{
	struct big b = {{x, x + 1, x * 2, x * 3, -x, 7}};

	return b;
}

__attribute__((noinline)) long sw(int k, long x)
{
	switch (k) {
	case 0: return x + 11;
	case 1: return x * 3 - 4;
	case 2: return x ^ 0x55;
	case 3: return x * x;
	case 4: return -x;
	case 5: return x << 3;
	case 6: return x % 7;
	case 7: return x / 3 + 100;
	}
	return 0;
}

int main(void)
{
	long t = mk(4).a[3];
	int i;

	for (i = 0; i < 9; ++i) {
		t = t * 3 + sw(i, t % 1000 + i);
	}
	printf("%ld\n", t);
	return 0;
}
EOF2
# what follows each ret, indirect jmp and jump to the return thunk: "kind next" per line
follows() {
	awk '
		pend != "" && NF > 0 && $1 !~ /^\./ && $1 !~ /:$/ { print pend, $1; pend = "" }
		$1 == "ret" || $1 == "retl" || $1 == "retq" { pend = "ret"; next }
		$1 == "jmp" && ($2 ~ /^\*/ || $2 ~ /^__x86_indirect_thunk_/) { pend = "jmp"; next }
		$1 == "jmp" && $2 ~ /^__x86_return_thunk/ { pend = "rethunk"; next }
		END { if (pend != "") print pend, "end" }' "$1"
}
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/b.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		for path in mir legacy; do
			run="env"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			for f in all return indirect-jmp none ""; do
				for extra in "" -mindirect-branch=thunk-extern -mfunction-return=thunk-extern; do
					if [ -n "$f" ]; then sls=-mharden-sls=$f; else sls=""; fi
					what="-m$m -$o $path [$sls] [$extra]"
					# shellcheck disable=SC2086
					if ! $run "$CC_BIN" -m$m -$o $sls $extra -S -o "$CC_TMP/b.s" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
						echo "run_harden_sls: $what: $(head -1 "$CC_TMP/c.err")"
						exit 1
					fi
					follows "$CC_TMP/b.s" >"$CC_TMP/f.txt"
					case "$f" in
					all) ret=1 jmp=1 ;;
					return) ret=1 jmp=0 ;;
					indirect-jmp) ret=0 jmp=1 ;;
					*) ret=0 jmp=0 ;;
					esac
					bad=$(awk -v r=$ret -v j=$jmp '
						$1 == "ret" && (($2 == "int3") != r) { print; exit }
						$1 == "jmp" && (($2 == "int3") != j) { print; exit }
						$1 == "rethunk" && $2 == "int3" { print; exit }' "$CC_TMP/f.txt")
					if [ -n "$bad" ]; then
						echo "run_harden_sls: $what: [$bad]"
						exit 1
					fi
					# (the legacy emitters make no jump table: a switch is compares there)
					if [ -z "$extra" ] && { ! grep -q '^ret' "$CC_TMP/f.txt" ||
						{ [ $path = mir ] && ! grep -q '^jmp' "$CC_TMP/f.txt"; }; }; then
						echo "run_harden_sls: $what: no ret or no indirect jmp to check"
						exit 1
					fi
					if [ -z "$extra" ]; then
						gcc -m$m -c -o "$CC_TMP/b.o" "$CC_TMP/b.s"
						gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/b.o"
						got=$("$CC_TMP/c")
						if [ "$got" != "$want" ]; then
							echo "run_harden_sls: $what prints [$got], gcc's [$want]"
							exit 1
						fi
					fi
				done
			done
		done
	done
done
if "$CC_BIN" -mharden-sls=bogus -S -o "$CC_TMP/n.s" "$CC_TMP/b.c" 2>"$CC_TMP/c.err" ||
	! grep -q 'mharden-sls' "$CC_TMP/c.err"; then
	echo "run_harden_sls: -mharden-sls=bogus was taken"
	exit 1
fi
echo "run_harden_sls: ok"
