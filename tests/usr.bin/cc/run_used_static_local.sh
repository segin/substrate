#!/bin/sh
# A block-scope static marked used is in the object, and so is what its
# initializer names, as with gcc, whether or not the code reads it -- in
# a statement expression too, and in an always_inline function inlined
# into one that is kept; not in a static inline function nothing calls.
# Linux's static_call() names its key only so (__ADDRESSABLE), and
# objtool, which finds the key by that reference, failed every x86-64
# object with a static call.  Needs the host's gcc.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
HOSTCC=${HOSTCC:-gcc}
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/used.c" <<'EOT'
extern int k1, k2, k3, k4;
#define KEEP(k) ({ static void *__attribute__((used, section(".discard.addressable"))) a_##k = (void *)&k; 0; })

void plain(void) {
    static void *__attribute__((used)) p = &k1;
}

static inline __attribute__((always_inline)) int helper(void) {
    return KEEP(k2);
}

int caller(void) {
    return helper();
}

static inline int dead(void) {
    return KEEP(k3);
}

void stmt(void) {
    KEEP(k4);
}
EOT

for m in 64 32; do
	if ! "$HOSTCC" -m$m -c -o "$CC_TMP/used_gcc.o" "$CC_TMP/used.c" 2>/dev/null; then
		echo "run_used_static_local: -m$m skipped (no host compiler for it)"
		continue
	fi
	for o in O0 O2; do
		"$HOSTCC" -m$m -$o -fno-pie -c -o "$CC_TMP/used_gcc.o" "$CC_TMP/used.c"
		"$CC_BIN" -m$m -$o -c -o "$CC_TMP/used_cc.o" "$CC_TMP/used.c"
		nm -u "$CC_TMP/used_gcc.o" | awk '{ print $2 }' | sort > "$CC_TMP/used_gcc.txt"
		nm -u "$CC_TMP/used_cc.o" | awk '{ print $2 }' | sort > "$CC_TMP/used_cc.txt"
		if ! cmp -s "$CC_TMP/used_gcc.txt" "$CC_TMP/used_cc.txt"; then
			echo "run_used_static_local: -m$m -$o undefined symbols differ from gcc's (< gcc, > cc):"
			diff "$CC_TMP/used_gcc.txt" "$CC_TMP/used_cc.txt" || true
			exit 1
		fi
	done
done
echo "run_used_static_local: ok"
