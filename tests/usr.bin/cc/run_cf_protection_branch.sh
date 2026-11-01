#!/bin/sh
# -fcf-protection=branch (and =full, and the bare option) puts an endbr
# where an indirect branch may land: each function's entry, and each
# label whose address is taken.  cc took the option and ignored it, and
# a kernel built with CONFIG_X86_KERNEL_IBT faults on the first indirect
# call on a machine with CET; objtool warned of !ENDBR.  Every function
# of the object begins with endbr64 (endbr32 at -m32); a computed
# goto's targets have one; __CET__ is gcc's; =none and no option have
# none; and the program runs as gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v objdump >/dev/null 2>&1; then
	echo "run_cf_protection_branch: skip (no gcc or objdump)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/b.c" <<'EOF2'
#include <stdio.h>

typedef int (*op_t)(int);
static int twice(int x) { return x * 2; }
static int thrice(int x) { return x * 3; }
int pub(int x) { return x + 1; }
op_t ops[] = { twice, thrice, pub };

__attribute__((noinline)) int run(const unsigned char *code, int x)
{
	static void *const table[] = { &&op0, &&op1, &&done };

	goto *table[*code++];
op0:
	x = ops[0](x);
	goto *table[*code++];
op1:
	x = ops[1](x);
	goto *table[*code++];
done:
	return ops[2](x);
}

int main(void)
{
	static const unsigned char prog[] = { 0, 1, 0, 2 };

	printf("%d\n", run(prog, 5));
	return 0;
}
EOF2
for m in 64 32; do
	gcc -m$m -O1 -o "$CC_TMP/g" "$CC_TMP/b.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	if [ $m = 64 ]; then endbr=endbr64; else endbr=endbr32; fi
	for o in O0 O2; do
		for f in -fcf-protection=branch -fcf-protection=full -fcf-protection; do
			if ! "$CC_BIN" -m$m -$o $f -c -o "$CC_TMP/b.o" "$CC_TMP/b.c" 2>"$CC_TMP/c.err"; then
				echo "run_cf_protection_branch: -m$m -$o $f: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			# the first instruction of each function
			bad=$(objdump -d --no-show-raw-insn "$CC_TMP/b.o" | awk -v e="$endbr" '
				/^[0-9a-f]+ <[^>]+>:$/ { name = $2; first = 1; next }
				first && NF > 1 { if ($2 != e) print name; first = 0 }')
			if [ -n "$bad" ]; then
				echo "run_cf_protection_branch: -m$m -$o $f: no $endbr at $bad"
				exit 1
			fi
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/b.o"
			got=$("$CC_TMP/c")
			if [ "$got" != "$want" ]; then
				echo "run_cf_protection_branch: -m$m -$o $f prints [$got], gcc's [$want]"
				exit 1
			fi
		done
		# a computed goto's targets: an endbr after each of their labels
		"$CC_BIN" -m$m -$o -fcf-protection=branch -S -o "$CC_TMP/b.s" "$CC_TMP/b.c"
		n=$(awk -v e="$endbr" '/^\.L.*:$/ { l = 1; next } l && $1 == e { c++ } { l = 0 } END { print c + 0 }' "$CC_TMP/b.s")
		if [ "$n" -lt 3 ]; then
			echo "run_cf_protection_branch: -m$m -$o: $n of the 3 goto targets have an $endbr"
			exit 1
		fi
		for f in "" -fcf-protection=none -fcf-protection=return; do
			# shellcheck disable=SC2086
			"$CC_BIN" -m$m -$o $f -S -o "$CC_TMP/n.s" "$CC_TMP/b.c"
			if grep -q endbr "$CC_TMP/n.s"; then
				echo "run_cf_protection_branch: -m$m -$o [$f] has an endbr"
				exit 1
			fi
		done
	done
done
for f in -fcf-protection=branch -fcf-protection=return -fcf-protection=full -fcf-protection -fcf-protection=none; do
	want=$(gcc $f -dM -E - </dev/null | grep '__CET__' || true)
	got=$("$CC_BIN" $f -dM -E - </dev/null | grep '__CET__' || true)
	if [ "$got" != "$want" ]; then
		echo "run_cf_protection_branch: $f defines [$got], gcc [$want]"
		exit 1
	fi
done
echo "run_cf_protection_branch: ok"
