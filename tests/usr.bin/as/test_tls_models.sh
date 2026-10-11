#!/bin/sh
# Programs that use thread-local storage, in each model a compiler has
# (the demonstration of AS-T-202 and AS-T-204).
#
# A shared library with a __thread variable of its own and one it
# exports, and a program that uses both, are compiled to assembly by the
# host's gcc -- for 32 and 64 bits, in the global-dynamic, local-dynamic
# and initial-exec models, with the traditional sequences and with TLS
# descriptors -- and assembled by this assembler; the host's gcc links
# them, and the program must print what the same program prints built by
# the host's toolchain alone.
#
# The relocation types had been right for some while and no such program
# ran.  A symbol defined in a thread-local section was left an object,
# and one defined elsewhere and reached by a thread-local relocation was
# left of no type, so the linker took each for an address where it is an
# offset in the thread's block, and the program read some other
# variable: it printed 187494 where it should have printed 10.  And
# `rex64`, which a compiler writes before the call of __tls_get_addr to
# make the sequence the length a linker expects, was no instruction.
#
# A combination the host cannot build -- no 32-bit libraries, a gcc
# without -mtls-dialect=gnu2 -- is skipped, and said to be.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

cat > lib.c <<'SRC'
__thread int counter = 5;
static __thread int hidden = 7;
extern int shared_global;
int bump(void) { counter += 1; hidden += 2; return counter + hidden + shared_global; }
int *where(void) { return &counter; }
SRC
cat > main.c <<'SRC'
#include <stdio.h>
extern __thread int counter;
int shared_global = 100;
int bump(void);
int *where(void);
static __thread int mine = 3;
int main(void) {
    int a = bump();
    int b = bump();

    mine += counter;
    printf("%d %d %d %d\n", a, b, mine, *where() == counter);
    return 0;
}
SRC

fail=0
ran=0
for m in 32 64; do
    # What the host's toolchain makes of it.
    if ! gcc -m$m -O1 -fPIC -shared -o libref.so lib.c 2> /dev/null ||
       ! gcc -m$m -O1 -o ref main.c -L. -lref -Wl,-rpath,"$work" 2> /dev/null; then
        echo "skip  -m$m: the host's gcc cannot build a -m$m program"
        continue
    fi
    want=$(./ref)
    for model in global-dynamic local-dynamic initial-exec; do
        for dialect in "" "-mtls-dialect=gnu2"; do
            name="-m$m -ftls-model=$model $dialect"
            # shellcheck disable=SC2086
            if ! gcc -m$m -O1 -fPIC -ftls-model=$model $dialect -S -o lib.s lib.c 2> /dev/null ||
               ! gcc -m$m -O1 -S -o main.s main.c 2> /dev/null; then
                echo "skip  $name: the host's gcc does not take these options"
                continue
            fi
            rm -f lib.o main.o libt.so prog
            if ! "$AS" --$m -o lib.o lib.s 2> err || ! "$AS" --$m -o main.o main.s 2>> err; then
                echo "FAIL  $name: not assembled: $(head -1 err | cut -c1-100)"; fail=1; continue
            fi
            if ! gcc -m$m -shared -o libt.so lib.o 2> err ||
               ! gcc -m$m -o prog main.o -L. -lt -Wl,-rpath,"$work" 2>> err; then
                echo "FAIL  $name: not linked: $(grep -v '^$' err | head -1 | cut -c1-100)"; fail=1; continue
            fi
            got=$(timeout 20 ./prog 2>&1)
            if [ "$got" != "$want" ]; then
                echo "FAIL  $name: printed '$got', not '$want'"; fail=1; continue
            fi
            ran=$((ran + 1))
        done
    done
done

[ "$ran" -gt 0 ] || { echo "FAIL: no combination could be tried"; exit 1; }
[ "$fail" -eq 0 ] && echo "ok: thread-local storage ($ran combinations run)"
exit "$fail"
