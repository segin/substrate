#!/bin/sh
# `.weakref alias, target`, held to GNU as.
#
# The alias is a name for a weak reference to the target: what refers to
# the alias is a relocation against the target, and the alias is no
# symbol of the object.  Referred to through such aliases alone, the
# target is weak and undefined (a program linked without it reads 0, and
# is not refused); named anywhere else in the file it is a strong
# reference, and defined in the file it is the definition.  An alias of
# an alias is one of what that is of.  gcc writes the directive for
# `static T x __attribute__((weakref("y")))`, and so does substrate's cc
# -- libgcc's gthr-posix.h reaches the pthread functions so -- and this
# assembler refused it as a directive it did not know: every such file
# assembled with GNU as and with no other.
#
# Each source is assembled by GNU as and by this one, for --32 and --64:
# the symbols of the two objects (binding, type, defined or not; the
# alias must not be there) and their relocations (type, symbol, addend)
# must be the same, and where GNU as refuses the source this one must
# refuse it at the same line with the same words.  Then a C file of
# weak references, compiled to assembly by gcc and by substrate's cc
# (built here, for the host), -m32 and -m64, at -O0 and -O2, with and
# without -fPIC, is assembled by this assembler: its symbols are bound
# as GNU as binds them from the same assembly, and the object, linked
# with and without definitions of what it refers to (as a program, or a
# shared object under -fPIC), prints what gcc's own build prints.
#
# Run by run-suite.sh, which sets $AS.  GNU as is the judge: without it
# (or gcc, readelf, make) there is nothing to compare, and the test
# fails rather than pass having tried nothing.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
GNU_AS=${GNU_AS:-as}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

for tool in gcc readelf make; do
    if ! command -v "$tool" > /dev/null 2>&1; then
        echo "FAIL: no $tool"
        exit 1
    fi
done
if ! "$GNU_AS" --version 2> /dev/null | grep -q 'GNU assembler'; then
    echo "FAIL: GNU as is the reference, and $GNU_AS is not it"
    exit 1
fi

fail=0
cases=0

# symbols OBJECT: each symbol but the sections, the file and the null
# one: value, type, binding, visibility, where (UND, ABS, COM or a
# section) and name.  _GLOBAL_OFFSET_TABLE_, which GNU as names where a
# relocation of the GOT is made and this assembler does not, is left out.
symbols() {
    readelf -sW "$1" |
        awk 'NR > 3 && $4 != "SECTION" && $4 != "FILE" && $8 != "" && $8 != "_GLOBAL_OFFSET_TABLE_" {
                 n = $7; if (n != "UND" && n != "ABS" && n != "COM") n = "sec"
                 print $3, $4, $5, $6, n, $8 }' | sort
}

# relocations OBJECT: where, type, symbol and addend of each, by where.
# (GNU as puts a relaxed jump's after the rest, and this assembler in
# order: the same relocations.)
relocations() {
    readelf -rW "$1" | awk '/R_/ { print $1, $3, $5, $6, $7 }' | sort
}

# refusal ERRFILE: the line and the words of the first error.
refusal() {
    grep -m1 -o ':[0-9]*: Error: .*' "$1"
}

# check MODES SOURCE: the lines of a source, `;` between them, assembled
# for each of MODES (32, 64) by both.
check() {
    for m in $1; do
        cases=$((cases + 1))
        printf '%s\n' "$2" | tr ';' '\n' > t.s
        rm -f g.o o.o
        "$GNU_AS" "--$m" -o g.o t.s 2> g.err
        g=$?
        "$AS" "--$m" -o o.o t.s 2> o.err
        o=$?
        if [ "$g" -ne 0 ]; then
            if [ "$o" -eq 0 ]; then
                echo "FAIL --$m [$2]: assembled; GNU as said: $(refusal g.err)"
                fail=1
            elif [ "$(refusal o.err)" != "$(refusal g.err)" ]; then
                echo "FAIL --$m [$2]: said [$(refusal o.err)]; GNU as [$(refusal g.err)]"
                fail=1
            fi
            continue
        fi
        if [ "$o" -ne 0 ]; then
            echo "FAIL --$m [$2]: refused: $(head -1 o.err)"
            fail=1
            continue
        fi
        if [ "$(symbols o.o)" != "$(symbols g.o)" ]; then
            echo "FAIL --$m [$2]: symbols"
            echo "  ours: $(symbols o.o | tr '\n' '|')"
            echo "  GNU:  $(symbols g.o | tr '\n' '|')"
            fail=1
        fi
        if [ "$(relocations o.o)" != "$(relocations g.o)" ]; then
            echo "FAIL --$m [$2]: relocations"
            echo "  ours: $(relocations o.o | tr '\n' '|')"
            echo "  GNU:  $(relocations g.o | tr '\n' '|')"
            fail=1
        fi
    done
}

both="32 64"
# Referred to through the alias alone: weak.
check "$both" '.weakref a, t;call a'
check "$both" 'call a;.weakref a, t'
check "$both" '.weakref a, t;.long a+4;.long a-.;jmp a'
check "$both" '.weakref a, t;.weakref b, t;call b'
check "$both" '.weakref a, t'
# What is said of the alias is said of nothing.
check "$both" '.weakref a, t;.globl a;.type a, @function;.weak a;call a'
# The target named itself, before or after, in an instruction or a
# directive: strong.
check "$both" '.weakref a, t;call a;call t'
check "$both" 'call t;.weakref a, t;call a'
check "$both" '.weakref a, t;.globl t;call a'
check "$both" '.weakref a, t;.type t, @function;call a'
check "$both" '.weakref a, t;.hidden t;call a'
check "$both" '.weakref a, t;.size t, 4;call a'
check "$both" '.weakref a, t;.set x, t;call a'
check "$both" '.weak t;.weakref a, t;call a;call t'
check "$both" '.weakref a, t;.comm t, 4;call a'
# Defined here: the definition.
check "$both" '.weakref a, t;call a;t: ret'
check "$both" '.weakref a, t;call a;.globl t;t: ret'
check "$both" '.weakref a, .Lx;call a;.Lx: ret'
check "$both" '.weakref a, t;.data;.globl t;.long a;t: .long 0'
# Chains, in either order.
check "$both" '.weakref a, b;.weakref b, c;call a;.long b'
check "$both" '.weakref a, b;call a;.weakref b, c'
check "$both" '.weakref b, c;.weakref a, b;call a;call c'
# A name a `.set` lent a value.
check "$both" '.set a, 1;.weakref a, t;call a'
# The operators of the GOT and the PLT.
check 64 '.weakref a, t;call a@PLT;mov a@GOTPCREL(%rip), %rax;mov a(%rip), %rax;leaq a+8(%rip), %rdx;.quad a'
check 64 '.weakref a, t;jmp a@PLT;movq a@GOTPCREL(%rip), %rax;call t@PLT'
check 32 '.weakref a, t;call a@PLT;mov a@GOT(%ebx), %eax;mov a@GOTOFF(%ebx), %eax;movl $a, %eax;.long a@GOTOFF'
check 32 '.weakref a, t;leal a@GOTOFF(%ebx), %eax;pushl $a+4'
# Refused, as GNU as refuses.
check "$both" '.weakref a, a'
check "$both" '.weakref a, b;.weakref b, a'
check "$both" '.weakref a, b;.weakref b, c;.weakref c, a'
check "$both" 'a: .weakref a, t'
check "$both" '.weakref a, t;a: ret'
check "$both" '.weakref a, t;.weakref a, u'
check "$both" '.weakref a, t;.set a, 1'
check "$both" '.weakref a, t;.equ a, 1'
check "$both" '.equiv a, 1;.weakref a, t'
check "$both" '.weakref a'
check "$both" '.weakref a, 1'
check "$both" '.weakref a, t+1'
check "$both" '.weakref a, t, u'
check "$both" '.weakref 1, t'

# A C file of weak references, through both compilers.
cat > u.c <<'EOF'
static int rx __attribute__((weakref("ry")));
static void rf(void) __attribute__((weakref("rg")));
static int rh(void) __attribute__((weakref, alias("rk")));
static int unref __attribute__((weakref("unref_target")));
extern int strong_y;
static int sy __attribute__((weakref("strong_y")));
int *rx_p = &rx;

int gr(void) { return &rx ? rx : -1; }
int cr(void) { int n = 0; if (rf) { rf(); n = 1; } return rh ? n + rh() : n - 2; }
int gs(void) { return sy + strong_y; }
int gp(void) { return rx_p != 0; }
EOF
cat > defs.c <<'EOF'
int ry = 21, strong_y = 3;
void rg(void) {}
int rk(void) { return 22; }
EOF
cat > strong.c <<'EOF'
int strong_y = 3;
EOF
cat > main.c <<'EOF'
#include <stdio.h>
int gr(void), cr(void), gs(void), gp(void);
int main(void)
{
	printf("%d %d %d %d\n", gr(), cr(), gs(), gp());
	return 0;
}
EOF

# c_symbols OBJECT: the symbols of u.c, its aliases among them (which
# must not be there).  (Not the rest: -m32 -fPIC's
# __x86.get_pc_thunk.ax, the signature of its group, is made an
# undefined symbol as well as the defined one by this assembler, with or
# without a `.weakref` -- a fault of its own.)
c_symbols() {
    symbols "$1" | grep -E ' (r[xfhgky]|unref|unref_target|strong_y|sy|rx_p|g[rsp]|cr)$'
}

# substrate's cc, for the host, out of the tree.
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 2)}
srcs=$(printf 'print-srcs:\n\t@echo $(CC_SRCS)\n' |
    make -s -C "$top/usr.bin/cc" -f Makefile -f - NATIVE_BUILD=1 print-srcs 2> /dev/null)
if [ -z "$srcs" ]; then
    echo "FAIL: the sources of cc are not to be found"
    exit 1
fi
mkdir cc-o
# shellcheck disable=SC2086
if ! (cd "$top/usr.bin/cc" && printf '%s\n' $srcs |
    xargs -P "$jobs" -n 1 sh -c 'cc -O1 -w -Iinclude -DCC_NATIVE_BUILD=1 -c -o "$0/$(echo "$1" | tr / _).o" "$1"' \
        "$work/cc-o") 2> cc.err ||
    ! cc -o subcc cc-o/*.o -lm 2>> cc.err; then
    echo "FAIL: cc does not build for the host: $(head -3 cc.err)"
    exit 1
fi

for m in 64 32; do
    if ! gcc -m$m -O2 -fno-pie -no-pie -o g0 main.c u.c strong.c 2> /dev/null; then
        echo "FAIL: gcc cannot build a -m$m program"
        fail=1
        continue
    fi
    want_none=$(./g0)
    gcc -m$m -O2 -fno-pie -no-pie -o g1 main.c u.c defs.c
    want_defs=$(./g1)
    gcc -m$m -fPIC -shared -o libstrong.so strong.c
    gcc -m$m -fPIC -shared -o libgd.so defs.c
    gcc -m$m -O2 -fPIC -shared -o libg.so u.c
    gcc -m$m -o gs main.c ./libg.so ./libstrong.so
    want_so=$(LD_LIBRARY_PATH=. ./gs)
    gcc -m$m -o gsd main.c ./libg.so ./libgd.so
    want_sod=$(LD_LIBRARY_PATH=. ./gsd)
    for comp in gcc cc; do
        for o in -O0 -O2; do
            for pic in -fno-pie -fPIC; do
                what="$comp -m$m $o $pic"
                cases=$((cases + 1))
                if [ $comp = gcc ]; then
                    gcc -m$m $o $pic -S -o u.s u.c
                elif ! ./subcc -m$m $o $pic -S -o u.s u.c 2> c.err; then
                    echo "FAIL $what: cc refused: $(head -1 c.err)"
                    fail=1
                    continue
                fi
                if ! grep -q '\.weakref' u.s; then
                    echo "FAIL $what: no .weakref in the assembly"
                    fail=1
                fi
                if ! "$AS" --$m -o o.o u.s 2> o.err; then
                    echo "FAIL $what: refused: $(head -1 o.err)"
                    fail=1
                    continue
                fi
                "$GNU_AS" --$m -o g.o u.s
                if [ "$(c_symbols o.o)" != "$(c_symbols g.o)" ]; then
                    echo "FAIL $what: symbols"
                    echo "  ours: $(c_symbols o.o | tr '\n' '|')"
                    echo "  GNU:  $(c_symbols g.o | tr '\n' '|')"
                    fail=1
                fi
                rm -f c0 c1 cs csd libo.so
                if [ $pic = -fPIC ]; then
                    if ! gcc -m$m -shared -o libo.so o.o 2> ld.err; then
                        echo "FAIL $what: the shared object does not link: $(head -1 ld.err)"
                        fail=1
                        continue
                    fi
                    gcc -m$m -o cs main.c ./libo.so ./libstrong.so 2> /dev/null
                    got=$(LD_LIBRARY_PATH=. ./cs 2>&1)
                    if [ "$got" != "$want_so" ]; then
                        echo "FAIL $what: shared, no definitions, prints [$got], gcc's [$want_so]"
                        fail=1
                    fi
                    gcc -m$m -o csd main.c ./libo.so ./libgd.so 2> /dev/null
                    got=$(LD_LIBRARY_PATH=. ./csd 2>&1)
                    if [ "$got" != "$want_sod" ]; then
                        echo "FAIL $what: shared, defined, prints [$got], gcc's [$want_sod]"
                        fail=1
                    fi
                    continue
                fi
                if ! gcc -m$m -no-pie -o c0 main.c o.o strong.c 2> ld.err; then
                    echo "FAIL $what: does not link without the definitions: $(head -1 ld.err)"
                    fail=1
                else
                    got=$(./c0 2>&1)
                    if [ "$got" != "$want_none" ]; then
                        echo "FAIL $what: no definitions, prints [$got], gcc's [$want_none]"
                        fail=1
                    fi
                fi
                if ! gcc -m$m -no-pie -o c1 main.c o.o defs.c 2> ld.err; then
                    echo "FAIL $what: does not link with the definitions: $(head -1 ld.err)"
                    fail=1
                else
                    got=$(./c1 2>&1)
                    if [ "$got" != "$want_defs" ]; then
                        echo "FAIL $what: defined, prints [$got], gcc's [$want_defs]"
                        fail=1
                    fi
                fi
            done
        done
    done
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "ok: .weakref ($cases cases)"
