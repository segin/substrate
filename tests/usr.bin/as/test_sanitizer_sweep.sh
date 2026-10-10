#!/bin/sh
# Every mnemonic the x86 encoder names, with operands of every shape it
# might be given and mostly does not take, through a build of the
# assembler with AddressSanitizer and UndefinedBehaviorSanitizer.
#
# A line is assembled or it is refused.  Assembled is exit 0 and an
# object; refused is exit 1, a message, and no object.  Anything else is
# a failure: a report from either sanitizer, a signal, a hang, an object
# with an error, silence with a failure.  And the plain build, $AS, must
# do with each line what the sanitizer build did, to the byte: memory is
# laid out differently in the two, so a result that depends on what was
# never set shows as a difference between them.
#
# Three things are settled besides, without which a clean run would say
# nothing: that the sanitizers of this host report at all, that the build
# swept is one they instrumented, and -- after the sweep -- that lines
# whose fate is certain met it, and that lines of both fates were seen.
#
# sanitizer_sweep.c is the program that does the assembling.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
case $AS in /*) ;; *) AS=$(pwd)/$AS ;; esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

SAN="-O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined"
ASAN_OPTIONS=detect_leaks=0
export ASAN_OPTIONS

# --- the sanitizers report ---------------------------------------------
cat > canary.c <<'SRC'
#include <stdlib.h>
int main(int argc, char **argv) {
    volatile int n = argc - 2;
    char *p = malloc(4);
    volatile int big = 0x7fffffff;
    volatile int sum;
    if (argv[1][0] == 'a') {
        free(p);
        return p[n];                        /* read of a freed block */
    }
    sum = big + argc;                       /* signed overflow */
    return sum == 12345;
}
SRC
# shellcheck disable=SC2086
if ! ${CC:-cc} $SAN -o canary canary.c 2> build.err; then
    echo "FAIL: nothing builds with the sanitizers on this host ($(head -1 build.err))"
    exit 1
fi
./canary a > canary.out 2>&1
if [ $? -eq 0 ] || ! grep -q 'AddressSanitizer' canary.out; then
    echo "FAIL: AddressSanitizer does not report a read of a freed block"
    exit 1
fi
./canary u > canary.out 2>&1
if [ $? -eq 0 ] || ! grep -q 'runtime error' canary.out; then
    echo "FAIL: UndefinedBehaviorSanitizer does not report a signed overflow"
    exit 1
fi

# --- the build that is swept --------------------------------------------
# The assembler's main() is as_main() here, and the sweeping program's is
# the program's.
#
# One compiler for each source file, as many at a time as there are
# processors: instrumented, the assembler is most of this test's time.
INC="-I$here -idirafter $top/include -idirafter $top/sys -idirafter $top/sys/include -I$top/usr.lib/elfobj/src"
export SAN INC
mkdir obj
ncpu=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
# shellcheck disable=SC2086,SC2016
if ! ${CC:-cc} $SAN $INC -c -Dmain=as_main -o as_main.o "$top/usr.bin/as/as.c" 2> build.err ||
   ! ls "$top"/usr.bin/as/*.c "$top"/usr.lib/elfobj/src/*.c "$here/sanitizer_sweep.c" | grep -v '/as/as\.c$' |
        xargs -P "$ncpu" -n 1 sh -c '${CC:-cc} $SAN $INC -c -o "obj/$(basename "$0" .c).o" "$0"' 2>> build.err ||
   ! ${CC:-cc} $SAN -o sweep as_main.o obj/*.o 2>> build.err; then
    echo "FAIL: the assembler does not build with the sanitizers ($(head -1 build.err))"
    exit 1
fi
if ! nm sweep 2>/dev/null | grep -q '__asan_init'; then
    echo "FAIL: the build to be swept is not instrumented"
    exit 1
fi

# --- the lines -----------------------------------------------------------
grep -o 'streq_ci(insn->mnemonic, "[a-z0-9]*")' "$top/usr.bin/as/as_x86_encode.c" |
    sed 's/.*"\(.*\)".*/\1/' | sort -u > mnemonics
[ "$(wc -l < mnemonics)" -gt 400 ] || { echo "FAIL: the mnemonic list was not found in the encoder"; exit 1; }

# The operands, and the modes each set is tried in: none, each kind
# alone, two registers, and too many, in both; the narrow registers, a
# symbol and the x87 stack in 32-bit code; the wide registers, the vector
# registers with and without a mask, and an indexed address in 64-bit.
# A set costs a run of the assembler for every mnemonic, so each is in
# the one mode where it means most.
cat > shapes <<'SRC'
-32 -64:
-32 -64:$1
-32 -64:%eax
-32 -64:(%eax)
-32 -64:%eax, %ebx
-32 -64:%eax, %eax, %eax, %eax
-32:%al, %bl
-32:%ax, %bx
-32:sym
-32:%st(1)
-64:%rax, %r15
-64:%xmm0, %xmm1
-64:%ymm0, %ymm1, %ymm2
-64:%zmm0, %zmm1, %zmm2{%k1}{z}
-64:$1, 8(%rsp,%r15,8)
SRC

# A job is a mode, `n` for a source that ends in a newline and `-` for
# one that does not, and the source; tabs between.
while IFS= read -r mn; do
    while IFS=: read -r modes ops; do
        for mode in $modes; do
            printf '%s\tn\t%s %s\n' "$mode" "$mn" "$ops"
        done
    done < shapes
done < mnemonics > jobs

# What the lexer must not read past the end of: a pseudo-prefix that ends
# its line, a string or a comment that is never closed, a backslash last
# of all, an operand begun and not finished.  With a final newline and
# without.
cat > edges <<'SRC'
{vex}
{evex}
{disp8}
{disp32}
nop {vex}
{
{vex
}
.ascii "abc
.ascii "abc\
.ascii "
/* never closed
nop /*
nop #
nop ;
\
.byte '
.byte 'a
mov $
mov %
mov (
mov %eax,
.long
.
:
SRC
while IFS= read -r text; do
    printf '%s\tn\t%s\n' -64 "$text"
    printf '%s\t-\t%s\n' -64 "$text"
done < edges >> jobs

# Lines whose fate is not in doubt.  The second set is each instruction
# that takes an operand, written with none: the encoder read its first
# operand's register before counting its operands.
for mode in -32 -64; do
    for mn in nop ret hlt cld; do printf 'ok\t%s\tn\t%s\n' "$mode" "$mn"; done
    for mn in inc dec push pop neg not mul div call jmp; do printf 'refused\t%s\tn\t%s\n' "$mode" "$mn"; done
done > certain
cut -f2- certain >> jobs

# --- the sweep -----------------------------------------------------------
workers=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
[ "$workers" -ge 1 ] 2>/dev/null || workers=2
awk -v w="$workers" '{ print > ("chunk." (NR % w)) }' jobs
n=0
while [ "$n" -lt "$workers" ]; do
    if [ -f "chunk.$n" ]; then
        (mkdir "w$n" && cd "w$n" && ../sweep "$AS" < "../chunk.$n" > "../result.$n" 2> "../err.$n") &
    fi
    n=$((n + 1))
done
wait
cat result.* > results 2>/dev/null

# --- what was found --------------------------------------------------------
fail=0
runs=$(wc -l < results)
if [ "$runs" -ne "$(wc -l < jobs)" ]; then
    echo "FAIL: $(wc -l < jobs) lines were to be tried and $runs were: $(cat err.* 2>/dev/null | head -1)"
    fail=1
fi
if grep -q '^FAIL' results; then
    echo "FAIL: $(grep -c '^FAIL' results) of $runs lines:"
    grep '^FAIL' results | head -20 | awk -F'\t' '{ printf "  %s  [%s]: %s\n", $2, $4, substr($1, 6) }'
    fail=1
fi
while IFS='	' read -r want job; do
    grep -qxF "$want	$job" results ||
        { echo "FAIL: $job is not $want: $(grep -F "	$job" results | head -1 | cut -f1)"; fail=1; }
done < certain
oks=$(grep -c '^ok' results)
refusals=$(grep -c '^refused' results)
# Each mnemonic is given fifteen sets of operands and takes one or two
# of them at most: far more are refused than assembled, and of some six
# hundred mnemonics hundreds of lines are assembled.
if [ "$oks" -lt 300 ] || [ "$refusals" -lt "$oks" ]; then
    echo "FAIL: $oks lines assembled and $refusals refused: the sweep is not reaching the encoder"
    fail=1
fi

[ "$fail" -eq 0 ] && echo "ok: sanitizer sweep ($runs lines: $oks assembled, $refusals refused)"
exit "$fail"
