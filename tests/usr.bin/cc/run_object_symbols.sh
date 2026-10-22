#!/bin/sh
# A data object cc defines is an STT_OBJECT of its size in the symbol
# table, as gcc makes it: it was an untyped symbol of size 0, and Linux's
# scripts/sorttable, which looks for main_extable_sort_needed as an
# object, failed the vmlinux link.  Each object's type and size must be
# gcc's, at -m32 and -m64; needs the host's gcc (and its -m32 support,
# that width skipped without it).
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
HOSTCC=${HOSTCC:-gcc}
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat > "$CC_TMP/objsyms.c" <<'EOF'
unsigned int needed = 1;
static char buf[37];
int zeroed;
char *p = buf;
struct { int a; long b; } pair = {1, 2};
const char greeting[] = "hello";
long long big[3] = {1, 2, 3};
int counter(void) { static int calls; return ++calls; }
char *get(void) { return buf; }
EOF

# name, type and size of each object symbol (a static local's name is the compiler's own)
syms() {
	readelf -sW "$1" | awk '$4 == "OBJECT" && $8 !~ /calls/ { print $8, $4, $3 }' | sort
}

for m in 32 64; do
	if ! "$HOSTCC" -m$m -c -o "$CC_TMP/objsyms_gcc.o" "$CC_TMP/objsyms.c" 2>/dev/null; then
		echo "run_object_symbols: -m$m skipped (no host compiler for it)"
		continue
	fi
	"$CC_BIN" -m$m -c -o "$CC_TMP/objsyms_cc.o" "$CC_TMP/objsyms.c"
	syms "$CC_TMP/objsyms_gcc.o" > "$CC_TMP/objsyms_gcc.txt"
	syms "$CC_TMP/objsyms_cc.o" > "$CC_TMP/objsyms_cc.txt"
	if ! cmp -s "$CC_TMP/objsyms_gcc.txt" "$CC_TMP/objsyms_cc.txt"; then
		echo "run_object_symbols: -m$m object symbols differ from gcc's (< gcc, > cc):"
		diff "$CC_TMP/objsyms_gcc.txt" "$CC_TMP/objsyms_cc.txt" || true
		exit 1
	fi
done
echo "run_object_symbols: ok"
