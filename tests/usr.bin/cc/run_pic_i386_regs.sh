#!/bin/sh
# i386 -fPIC code goes through the register allocator: the GOT's address
# made once at entry by a pc thunk, data reached off it (@GOTOFF where it
# binds within the object, else its @GOT entry), and a call that may bind
# outside through @PLT with the GOT in %ebx.  Every -fPIC function used
# to go to the old emitter, a stack slot for every value.  A shared
# library built by cc, loaded by a gcc-built program, must print what the
# same library built by gcc does.  Needs a host gcc that builds -m32
# programs; skipped, with a note, if not.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! printf 'int main(void) { return 0; }\n' | gcc -m32 -x c - -o "$CC_TMP/probe" 2>/dev/null; then
	echo "run_pic_i386_regs: skip (gcc cannot build -m32 programs)"
	exit 0
fi
d="$CC_TMP/picr"
mkdir -p "$d"

cat >"$d/lib.c" <<'EOF2'
#include <stdio.h>
#include <string.h>

int lib_count = 5;
static int hidden_count = 7;
static const char *const names[] = {"zero", "one", "two", "three"};
extern int app_value(int);

static int __attribute__((noinline)) local_add(int a, int b) {
    return a + b + hidden_count;
}

int lib_mix(int k) {
    char buf[32];
    int t;

    /* data, a string table, a call into the program and one within the library, a libc call */
    snprintf(buf, sizeof(buf), "%s-%d", names[k & 3], lib_count);
    t = (int)strlen(buf) * 100 + app_value(k) + local_add(k, lib_count);
    hidden_count += k;
    lib_count += t & 7;
    return t;
}

int (*lib_pick(int k))(int) {
    return k ? lib_mix : app_value;
}

int lib_jump(int k) {
    void *where = k ? &&yes : &&no;

    goto *where;
yes:
    return 1 + hidden_count;
no:
    return 2 + lib_count;
}
EOF2
cat >"$d/main.c" <<'EOF2'
#include <stdio.h>

int lib_mix(int);
int (*lib_pick(int))(int);
int lib_jump(int);
extern int lib_count;

int app_value(int k) {
    return k * 3 + 1;
}

int main(void) {
    int a = lib_mix(1);
    int b = lib_mix(2);

    printf("%d %d %d %d %d %d\n", a, b, lib_pick(1)(3), lib_pick(0)(3), lib_jump(1), lib_jump(0));
    printf("%d\n", lib_count);
    return 0;
}
EOF2
gcc -m32 -O1 -fPIC -shared -o "$d/libg.so" "$d/lib.c"
gcc -m32 -O1 -o "$d/main_g" "$d/main.c" "$d/libg.so" -Wl,-rpath,"$d"
want=$("$d/main_g")
for o in O0 O2; do
	CC_MIR_STATS=1 "$CC_BIN" -m32 -$o -fPIC -c -o "$d/lib.o" "$d/lib.c" 2>"$d/stats"
	if grep "^mir: .*not taken" "$d/stats"; then
		echo "run_pic_i386_regs: -$o: the above do not go through the allocator"
		exit 1
	fi
	gcc -m32 -shared -o "$d/libc_$o.so" "$d/lib.o"
	if readelf -d "$d/libc_$o.so" | grep -q TEXTREL; then
		echo "run_pic_i386_regs: -$o: the library has text relocations"
		exit 1
	fi
	cp "$d/libc_$o.so" "$d/libg.so"
	got=$("$d/main_g")
	if [ "$got" != "$want" ]; then
		echo "run_pic_i386_regs: -$o prints"
		echo "$got"
		echo "where gcc's library prints"
		echo "$want"
		exit 1
	fi
done
echo "run_pic_i386_regs: ok"
