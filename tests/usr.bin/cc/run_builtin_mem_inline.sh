#!/bin/sh
# A __builtin_memcpy, __builtin_memmove or __builtin_memset of a constant
# size up to a few words calls no library function, as with gcc: Linux's
# vDSO, which has no memcpy to call, makes its unaligned accesses with
# __builtin_memcpy of 1 to 8 bytes, and its link failed on an undefined
# memcpy.  codegen/builtin_mem_inline.c checks the bytes they leave.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat >"$CC_TMP/mem.c" <<'EOF'
unsigned long long get8(const void *p) {
    unsigned long long v;

    __builtin_memcpy(&v, p, sizeof(v));
    return v;
}

void put4(void *p, unsigned v) {
    __builtin_memcpy(p, &v, sizeof(v));
}

void shift7(char *p) {
    __builtin_memmove(p, p + 1, 7);
}

void clear13(char *p) {
    __builtin_memset(p, 0, 13);
}
EOF
for m in 32 64; do
	for o in O0 O2; do
		"$CC_BIN" -m$m -$o -c -o "$CC_TMP/mem.o" "$CC_TMP/mem.c"
		if nm -u "$CC_TMP/mem.o" | grep -Eq 'mem(cpy|move|set)'; then
			echo "run_builtin_mem_inline: -m$m -$o calls the library:"
			nm -u "$CC_TMP/mem.o"
			exit 1
		fi
	done
done
echo "run_builtin_mem_inline: ok"
