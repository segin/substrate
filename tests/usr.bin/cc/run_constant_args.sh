#!/bin/sh
# A parameter of a static function that every call of it passes the same
# constant is that constant, and what only another value would run is
# gone -- here a call of a function no object defines -- as with gcc's
# interprocedural constant propagation at -O2: Linux's fpu__restore_sig()
# passes its static __fpu_restore_sig(), too large to inline, an
# ia32_fxstate it has made false on x86-64 without IA32 emulation, and
# the call of convert_to_fxsr() under it left vmlinux with that function
# undefined.  codegen/constant_args.c checks what such functions return.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/ipa.c" <<'EOF2'
extern void missing(void);
extern int other(int);

static int work(int *p, _Bool quirk) {
    int r = *p;

    if (quirk)
        missing();
    r += other(r);
    r ^= other(r + 1);
    return r;
}

int a(int *p, int frame) {
    _Bool quirk = 0;

    frame &= 0;
    if (frame)
        quirk = 1;
    return work(p, quirk);
}

int b(int *p) {
    return work(p, 0) * 2;
}

#define likely(x) __builtin_expect(!!(x), 1)

static _Bool restore(int *p, int *q, _Bool fx) {
    int r = *p;

    if (other(r))
        r = 3;
    if (likely(!fx))
        return other(r + *q);
    missing();
    return 0;
}

_Bool c(int *p, int frame) {
    _Bool fx = 0;
    _Bool ok;

    frame &= 0;
    if (frame && other(1))
        fx = 1;
    ok = restore(p, p, fx);
    if (!ok)
        other(5);
    return ok;
}
EOF2
for m in 32 64; do
	"$CC_BIN" -m$m -O2 -c -o "$CC_TMP/ipa.o" "$CC_TMP/ipa.c"
	if nm -u "$CC_TMP/ipa.o" | grep -q missing; then
		echo "run_constant_args: -m$m -O2 calls the function the constant argument rules out"
		exit 1
	fi
done
echo "run_constant_args: ok"
