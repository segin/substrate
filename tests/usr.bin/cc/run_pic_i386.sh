#!/bin/sh
# i386 position-independent code: cc -m32 -fPIC addressed every global
# and string absolutely (movl $sym), so a shared object it built needed
# text relocations, and Linux's i386 vDSO, which may have none, held an
# absolute relocation of vdso_u_time_data.  Data now comes from the GOT's
# address (a pc thunk and _GLOBAL_OFFSET_TABLE_) plus sym@GOTOFF for a
# symbol that binds within the object, or from sym@GOT; a call that may
# bind outside it goes through sym@PLT with the GOT in %ebx.
#
# A shared library built by cc, linked by the host's gcc, is loaded by a
# program and must behave as the same library built by gcc; its objects
# must hold no absolute relocation in code, and the library no TEXTREL.
# Needs a host gcc that builds -m32 programs; skipped, with a note, if not.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
HOSTCC=${HOSTCC:-gcc}
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
d="$CC_TMP/pic_i386"
mkdir -p "$d"

# Which symbols bind within the object, in the code cc writes (no host compiler needed):
# under `#pragma GCC visibility push(hidden)` (Linux's include/linux/hidden.h) every
# symbol, a prototype's call included, is reached directly; under -fPIE every symbol
# the file defines is, an external one through the GOT; under -fPIC a default-visibility
# definition may be preempted and goes through the GOT and the PLT.
cat > "$d/vis.c" <<'EOF'
#pragma GCC visibility push(hidden)
extern int ext_data;
int defined_data = 3;
extern void ext_fn(void);
void take(void (*fn)(void));
int f(void) { ext_fn(); take(ext_fn); return ext_data + defined_data; }
/* a block's declarations take no visibility from the pragma */
int g(int k) { int local = k; static int st; st += local; return st; }
#pragma GCC visibility pop
EOF
cat > "$d/pie.c" <<'EOF'
int def = 1;
extern int ext;
int use(void) { return def + ext; }
int caller(void) { return use(); }
EOF
"$CC_BIN" -m32 -O2 -fPIC -S -o "$d/vis.s" "$d/vis.c"
if grep -q '@GOT(\|@PLT' "$d/vis.s" || ! grep -q 'ext_data@GOTOFF' "$d/vis.s"; then
	echo "run_pic_i386: a hidden symbol under the visibility pragma is reached through the GOT or PLT"
	exit 1
fi
"$CC_BIN" -m32 -O2 -fPIE -S -o "$d/pie.s" "$d/pie.c"
if ! grep -q 'def@GOTOFF' "$d/pie.s" || ! grep -q 'ext@GOT(' "$d/pie.s" || grep -q '@PLT' "$d/pie.s"; then
	echo "run_pic_i386: -fPIE does not bind the file's own symbols within it"
	exit 1
fi
"$CC_BIN" -m32 -O2 -fPIC -S -o "$d/pic.s" "$d/pie.c"
if ! grep -q 'def@GOT(' "$d/pic.s" || ! grep -q 'use@PLT' "$d/pic.s"; then
	echo "run_pic_i386: -fPIC binds a default-visibility definition within the object"
	exit 1
fi

printf 'int main(void) { return 0; }\n' > "$d/probe.c"
if ! "$HOSTCC" -m32 -o "$d/probe" "$d/probe.c" >/dev/null 2>&1; then
	echo "run_pic_i386: skipped (no -m32 host compiler)"
	exit 0
fi

cat > "$d/lib.c" <<'EOF'
static int local_counter = 5;
int __attribute__((visibility("hidden"))) hidden_val = 7;
int exported_val = 11;
extern int main_owned;
static const char msg[] = "pic-ok";
static int table[4] = {1, 2, 3, 4};
int helper(int x) { return x * 3; }
static int lhelper(int x) { return x + 1; }
int __attribute__((visibility("hidden"))) hhelper(int x) { return x - 2; }
int lib_sum(int k) {
	local_counter += k;
	table[2] += k;
	return local_counter + hidden_val + exported_val + main_owned + table[2] + (int)sizeof(msg) + msg[0] +
	       helper(k) + lhelper(k) + hhelper(k);
}
const char *lib_msg(void) { return msg; }
int *lib_counter_addr(void) { return &local_counter; }
EOF
cat > "$d/main.c" <<'EOF'
#include <stdio.h>
int main_owned = 100;
int lib_sum(int k);
const char *lib_msg(void);
int *lib_counter_addr(void);
int main(void) {
	int a = lib_sum(1);
	int b = lib_sum(2);
	*lib_counter_addr() += 1000;
	printf("%d %d %d %s\n", a, b, lib_sum(0), lib_msg());
	return 0;
}
EOF

"$HOSTCC" -m32 -O2 -fPIC -shared -o "$d/libref.so" "$d/lib.c"
"$HOSTCC" -m32 -o "$d/ref" "$d/main.c" "$d/libref.so"
LD_LIBRARY_PATH="$d" "$d/ref" > "$d/ref.out"

for o in O0 O2; do
	"$CC_BIN" -m32 -$o -fPIC -c -o "$d/lib.o" "$d/lib.c"
	if readelf -r "$d/lib.o" | sed -n '/\.rel\.text/,/^$/p' | grep -q 'R_386_32 '; then
		echo "run_pic_i386: -$o: an absolute relocation in code:"
		readelf -r "$d/lib.o" | sed -n '/\.rel\.text/,/^$/p' | grep 'R_386_32 '
		exit 1
	fi
	"$HOSTCC" -m32 -shared -o "$d/libcc.so" "$d/lib.o"
	if readelf -d "$d/libcc.so" | grep -q TEXTREL; then
		echo "run_pic_i386: -$o: the shared object has text relocations"
		exit 1
	fi
	"$HOSTCC" -m32 -o "$d/prog" "$d/main.c" "$d/libcc.so"
	LD_LIBRARY_PATH="$d" "$d/prog" > "$d/cc.out"
	if ! cmp -s "$d/ref.out" "$d/cc.out"; then
		echo "run_pic_i386: -$o: the program reads otherwise than with gcc's library"
		diff "$d/ref.out" "$d/cc.out" || true
		exit 1
	fi
done
echo "run_pic_i386: ok"
