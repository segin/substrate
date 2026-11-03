#!/bin/sh
# The .dynstr index is let go when the dynamic sections are made.
#
# To keep each string in .dynstr once, the linker keeps an index of the
# strings already there, each a copy of its own.  It emptied the index
# when it began the next .dynstr, but not when it had made the last one:
# the index and every string in it were still held when the linker
# exited, which valgrind's "still reachable" counts against it.
#
# The linker is built with its .dynstr code's strdup() and free() counted:
# after it has made a PIE and a shared object, both importing from a
# shared object, no copy that code made is left.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

cd "$work" || exit 1

# strdup() and free() in ld_dynamic.c, counted
cat > count.h <<'EOF'
#include <stdlib.h>
#include <string.h>
char *ldtest_strdup(const char *s);
void ldtest_free(void *p);
#define strdup ldtest_strdup
#define free ldtest_free
EOF
cat > count.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void **live;
static size_t nlive, caplive;

char *ldtest_strdup(const char *s) {
    char *p = strdup(s);

    if (p != NULL && nlive == caplive) {
        size_t cap = caplive == 0 ? 64 : caplive * 2;
        void **next = realloc(live, cap * sizeof(*next));

        if (next == NULL) {
            abort();
        }
        live = next;
        caplive = cap;
    }
    if (p != NULL) {
        live[nlive++] = p;
    }
    return p;
}

void ldtest_free(void *p) {
    size_t i;

    for (i = 0; p != NULL && i < nlive; ++i) {
        if (live[i] == p) {
            live[i] = live[--nlive];
            break;
        }
    }
    free(p);
}

__attribute__((destructor)) static void ldtest_report(void) {
    const char *path = getenv("LDTEST_LIVE");
    FILE *f;

    if (path != NULL && (f = fopen(path, "w")) != NULL) {
        fprintf(f, "%lu\n", (unsigned long)nlive);
        fclose(f);
    }
}
EOF

inc="-idirafter $top/include -idirafter $top/sys -idirafter $top/sys/include -I$top/usr.lib/elfobj/src"
for src in "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c; do
    obj=$(basename "$src" .c).o
    case $src in
    */usr.bin/ld/*) obj=ld_$obj ;;
    esac
    extra=
    case $src in
    */usr.bin/ld/ld_dynamic.c) extra="-include $work/count.h" ;;
    esac
    # shellcheck disable=SC2086
    ${CC:-cc} -O0 -w $inc $extra -c -o "$obj" "$src" || {
        echo "FAIL: the linker does not build for the host"; exit 1; }
done
${CC:-cc} -O0 -w -c -o count.o count.c || exit 1
${CC:-cc} -o ld ./*.o || { echo "FAIL: the linker does not link for the host"; exit 1; }
mkdir -p bin && ln -sf "$work/ld" bin/ld

cat > lib.c <<'EOF'
int lib_one(void) { return 1; }
int lib_two(void) { return 2; }
EOF
cat > prog.c <<'EOF'
#include <stdio.h>
extern int lib_one(void);
extern int lib_two(void);
int main(void) { printf("%d\n", lib_one() + lib_two()); return 0; }
EOF
${GCC:-gcc} -O2 -fPIC -c -o lib.o lib.c || exit 1
${GCC:-gcc} -shared -o libt.so lib.o || exit 1
${GCC:-gcc} -O2 -fPIE -c -o prog.o prog.c || exit 1

# made: WHAT FILE -- LINK ARGS
made() {
    what=$1
    shift
    rm -f live
    if ! LDTEST_LIVE="$work/live" ${GCC:-gcc} -B"$work/bin/" "$@" > said 2>&1; then
        echo "FAIL: $what: $(head -1 said)"; fail=1; return
    fi
    if [ ! -s live ]; then
        echo "FAIL: $what: the counted linker did not report"; fail=1; return
    fi
    if [ "$(cat live)" != 0 ]; then
        echo "FAIL: $what: $(cat live) .dynstr index strings still held when the linker exits"; fail=1
    fi
}

made "a PIE" -pie -o prog prog.o -L. -lt
made "a shared object" -shared -o libu.so prog.o -L. -lt

if [ $fail -eq 0 ]; then
    echo "ok   test_dynstr_index_freed"
fi
exit $fail
