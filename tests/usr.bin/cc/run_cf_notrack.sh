#!/bin/sh
# Under -fcf-protection=branch an indirect branch IBT is not to check
# has gcc's `notrack` prefix: a switch's jump through its table (whose
# case labels have no endbr) and a call through a function type with
# the nocf_check attribute -- a pointer's, a typedef's, a member's --
# which under -mindirect-branch=thunk-extern is a call of the
# __x86_indirect_thunk_nt_<reg> thunk instead.  cc made every one a
# plain branch, so an IBT CPU faulted on the first switch through a
# table.  A function's own nocf_check leaves the calls of it alone;
# without -fcf-protection nothing is notrack; and the program runs as
# gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_cf_notrack: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/n.c" <<'EOF2'
#include <stdio.h>

typedef void (*nofn)(void) __attribute__((nocf_check));
typedef int fnt(int) __attribute__((nocf_check));

static int hits;
__attribute__((noinline)) void bump(void) { ++hits; }
__attribute__((noinline)) int twice(int x) { return 2 * x; }
__attribute__((nocf_check, noinline)) void own(void) { hits += 100; }

void (*plain_p)(void) = bump;
__attribute__((nocf_check)) void (*nocf_p)(void);
fnt *nocf_t;
nofn nocf_arr[2];
struct s { int k; void (*m)(void) __attribute__((nocf_check)); } st;

__attribute__((noinline)) int sw(int x)
{
	switch (x) {
	case 0: return hits + 11;
	case 1: return hits * 3;
	case 2: return hits - 7;
	case 3: return hits ^ 5;
	case 4: return hits + 9;
	case 5: return hits | 2;
	case 6: return hits + 40;
	case 7: return hits * 7;
	}
	return 0;
}
__attribute__((noinline)) void call_plain(void) { plain_p(); }
__attribute__((noinline)) void call_nocf(void) { nocf_p(); }
__attribute__((noinline)) int call_typedef(int x) { return nocf_t(x) + 1; }
__attribute__((noinline)) void call_arr(int i) { nocf_arr[i](); }
__attribute__((noinline)) void call_member(void) { st.m(); }
__attribute__((noinline)) void call_param(nofn f) { (*f)(); }
__attribute__((noinline)) void call_own(void) { own(); }

int main(void)
{
	int i, t = 0;

	/* (the nocf_check pointers are set through a cast: a plain function's type is not theirs) */
	nocf_p = (nofn)bump;
	nocf_t = (fnt *)twice;
	nocf_arr[0] = (nofn)bump;
	nocf_arr[1] = (nofn)bump;
	st.m = (nofn)bump;
	call_plain();
	call_nocf();
	call_arr(1);
	call_member();
	call_param((nofn)bump);
	call_own();
	for (i = 0; i < 9; ++i) {
		t += sw(i);
	}
	printf("%d %d %d\n", hits, call_typedef(20), t);
	return 0;
}
EOF2

# the functions that have a notrack branch in an assembly listing
notrack_fns() {
	awk '/^[A-Za-z_][A-Za-z0-9_]*:/ { fn = $1; sub(/:$/, "", fn) }
		/notrack|_thunk_nt_/ { print fn }' "$1" | sort -u | tr '\n' ' '
}

for m in 64 32; do
	gcc -m$m -O1 -fcf-protection=branch -o "$CC_TMP/g" "$CC_TMP/n.c" 2>/dev/null || continue
	want=$("$CC_TMP/g")
	for o in O0 O2; do
		for path in mir legacy; do
			run="env"
			if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
			if ! $run "$CC_BIN" -m$m -$o -fcf-protection=branch -S -o "$CC_TMP/c.s" "$CC_TMP/n.c" 2>"$CC_TMP/c.err"; then
				echo "run_cf_notrack: -m$m -$o $path: $(head -1 "$CC_TMP/c.err")"
				exit 1
			fi
			# each call through a nocf_check type is notrack, and no other call
			got=$(notrack_fns "$CC_TMP/c.s" | sed 's/sw //')
			case "$got" in
			"call_arr call_member call_nocf call_param call_typedef ") ;;
			*)
				echo "run_cf_notrack: -m$m -$o $path: notrack branches in [$got]"
				exit 1
				;;
			esac
			# a switch's jump through a table, where cc made one, is notrack
			if awk '/^sw:/ { on = 1; next } /^[A-Za-z_][A-Za-z0-9_]*:/ { on = 0 } on' "$CC_TMP/c.s" |
				grep -E 'jmp[a-z]* +\*' | grep -v notrack; then
				echo "run_cf_notrack: -m$m -$o $path: a switch's table jump without notrack"
				exit 1
			fi
			$run "$CC_BIN" -m$m -$o -fcf-protection=branch -c -o "$CC_TMP/c.o" "$CC_TMP/n.c"
			gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/c.o"
			if [ "$("$CC_TMP/c")" != "$want" ]; then
				echo "run_cf_notrack: -m$m -$o $path prints [$("$CC_TMP/c")], gcc's [$want]"
				exit 1
			fi
		done
		# no notrack without -fcf-protection
		"$CC_BIN" -m$m -$o -S -o "$CC_TMP/c.s" "$CC_TMP/n.c"
		if grep -q notrack "$CC_TMP/c.s"; then
			echo "run_cf_notrack: -m$m -$o: notrack without -fcf-protection"
			exit 1
		fi
	done
	# through the retpoline: the _nt_ thunk for nocf_check calls, the plain one for a table's jump
	"$CC_BIN" -m$m -O2 -fcf-protection=branch -mindirect-branch=thunk-extern -S -o "$CC_TMP/c.s" "$CC_TMP/n.c"
	if grep -q notrack "$CC_TMP/c.s" || ! grep -q '__x86_indirect_thunk_nt_' "$CC_TMP/c.s" ||
		awk '/^sw:/ { on = 1; next } /^[A-Za-z_][A-Za-z0-9_]*:/ { on = 0 } on' "$CC_TMP/c.s" | grep -q _thunk_nt_; then
		echo "run_cf_notrack: -m$m -mindirect-branch=thunk-extern: thunks not as gcc's"
		exit 1
	fi
done
echo "run_cf_notrack: ok"
