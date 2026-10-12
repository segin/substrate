#!/bin/sh
# The assembler assembles a compiler's output: its own.
#
# Every source of the assembler and of libelfobj is compiled to
# assembly by the host's compiler, assembled by this assembler, and
# linked; and the assembler that makes is run on a corpus, where it is
# to write the object this one writes.  At -O0 and at -O2, which are
# two kinds of source: one of stack slots and one of registers, jump
# tables and aligned loops.
#
# It is the test the others are not: they hold statements to what GNU
# as makes of them, a few at a time, and this holds a quarter of a
# million of them to running.  It found `.string ":"` assembled to
# nothing (the directive was taken for a label), which no statement of
# any corpus had, and that a current gcc writes `.base64`.
#
# Where there is no C compiler that makes assembly this assembler is
# for, the test says it was skipped and passes.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
case $AS in /*) ;; *) AS=$(pwd)/$AS ;; esac
CC=${HOSTCC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

# The mode the compiler makes code for, by what it defines.
printf '#ifdef __x86_64__\n64\n#elif defined(__i386__)\n32\n#else\nnone\n#endif\n' > mode.c
mode=$("$CC" -E -P mode.c 2> /dev/null | tr -d ' \n')
case $mode in
32 | 64) ;;
*)
    echo "ok: skipped, $CC makes no x86 assembly"
    exit 0
    ;;
esac
printf 'int main(void) { return 0; }\n' > probe.c
if ! "$CC" -no-pie -o probe probe.c 2> /dev/null; then
    echo "ok: skipped, $CC does not link a program here"
    exit 0
fi

flags="-std=gnu17 -w -fno-pie -idirafter $top/include -idirafter $top/sys -idirafter $top/sys/include"
flags="$flags -I$top/usr.lib/elfobj/src -I$top/usr.bin/as"
fail=0
cases=0
for opt in -O0 -O2; do
    cases=$((cases + 1))
    d=build$opt
    mkdir "$d"
    bad=
    for c in "$top"/usr.bin/as/*.c "$top"/usr.lib/elfobj/src/*.c; do
        b=$(basename "$c" .c)
        if ! "$CC" -S $opt $flags -o "$d/$b.s" "$c" 2> "$d/cc.err"; then
            bad="$b is not compiled: $(head -1 "$d/cc.err")"
            break
        fi
        if ! "$AS" --"$mode" -o "$d/$b.o" "$d/$b.s" 2> "$d/as.err"; then
            bad="$b.s is not assembled: $(head -1 "$d/as.err")"
            break
        fi
    done
    if [ -z "$bad" ] && ! "$CC" -no-pie -o "$d/as" "$d"/*.o 2> "$d/ld.err"; then
        bad="not linked: $(head -1 "$d/ld.err")"
    fi
    if [ -n "$bad" ]; then
        echo "FAIL $opt: $bad"
        fail=1
        continue
    fi
    # The assembler that was made, and this one, on the same sources.
    for src in "$here"/corpus/x86_64_v2_gas_all_valid_assembles.s "$d/as_parser.s" "$d/as_lower.s"; do
        m=$mode
        case $src in *corpus*) m=64 ;; esac
        "$AS" --$m -o want.o "$src" 2> /dev/null
        r1=$?
        "$d/as" --$m -o got.o "$src" 2> /dev/null
        r2=$?
        if [ "$r1" != "$r2" ] || { [ "$r1" = 0 ] && ! cmp -s want.o got.o; }; then
            echo "FAIL $opt: the assembler it makes does not assemble $(basename "$src") as this one does"
            fail=1
        fi
    done
done

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
