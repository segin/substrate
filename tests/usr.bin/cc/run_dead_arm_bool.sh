#!/bin/sh
# A bool a dead arm alone sets, passed to a function inlined, folds where
# the inlined body tests it -- through copies that change its size (a
# bool made an int and back) and once the arm is gone, which may be the
# last thing the optimizer drops.  Linux's fpu__restore_sig() sets
# ia32_fxstate only in `if (ia32_frame && use_fxsr())`, ia32_frame 0 on
# x86-64 without IA32_EMULATION, and __fpu_restore_sig(), inlined, calls
# convert_to_fxsr() -- not built in that kernel -- unless it is true: the
# link failed.  gcc drops the call; so must cc, at -O1, -O2 and -Os.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/d.c" <<'EOF2'
extern void undefined_only_if_reached(void);
extern int work(int);

static __attribute__((always_inline)) inline _Bool has_feature(void)
{
	asm goto("jmp %l[yes]" : : : : yes);
	return 0;
yes:
	return 1;
}

static int restore(int x, _Bool ia32)
{
	int r = work(x);

	if (has_feature())
		r += work(2);
	if (__builtin_expect(!ia32, 1))
		return r + work(r);
	undefined_only_if_reached();
	return work(r * 3) + work(r + 7) + work(r ^ 5) + work(r - 9) + work(r | 3);
}

int entry(int x, int frame)
{
	_Bool ia32 = 0;

	frame &= 0;
	if (frame && has_feature())
		ia32 = 1;
	return restore(x, ia32);
}
EOF2
if command -v gcc >/dev/null 2>&1 && gcc -O2 -S -o "$CC_TMP/g.s" "$CC_TMP/d.c" 2>/dev/null &&
	grep -q undefined_only_if_reached "$CC_TMP/g.s"; then
	echo "run_dead_arm_bool: skip (gcc keeps the call)"
	exit 0
fi
for m in 64 32; do
	for o in O1 O2 Os; do
		"$CC_BIN" -m$m -$o -S -o "$CC_TMP/d.s" "$CC_TMP/d.c"
		if grep -q undefined_only_if_reached "$CC_TMP/d.s"; then
			echo "run_dead_arm_bool: -m$m -$o keeps the call of the undefined function"
			exit 1
		fi
	done
done
echo "run_dead_arm_bool: ok"
