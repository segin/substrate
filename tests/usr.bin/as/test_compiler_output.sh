#!/bin/sh
# Does the assembler assemble what a compiler writes?  (AS-T-019.)
#
# compiler/prog.c is compiled to assembly by the host's gcc, for 32 and 64
# bits and at several settings; this assembler assembles it; the host's
# gcc links the object; the program is run and its output compared with
# that of the same program built by the host's toolchain alone.  Each
# combination ends one of four ways:
#
#   AS     the assembler refused the source (its first complaint is shown)
#   LINK   the object would not link
#   WRONG  the program ran and did not print what it should, or died
#   RUNS   the same output
#
# The last two settings are there because distributions differ in whether
# their gcc protects indirect branches by default -- Ubuntu's and Fedora's
# do, writing endbr64 at the head of every function and `notrack` before a
# jump through a table -- so that either kind of host tries both kinds of
# output.
#
# compiler/expect-runs lists the combinations that must RUN; one that does
# not fails the test.  The rest are reported and do not fail it: what a
# compiler writes depends on the compiler, and this runs on whichever gcc
# the host has.  When a combination comes to run here and on the CI host,
# its line is added to the file, and from then on it is held.
#
# A held combination that could not be tried -- no gcc, no 32-bit
# libraries, an option this gcc does not have -- fails the test too.
# Nothing was shown about it, and a test that passes having tried nothing
# is worse than one that fails.
#
# Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
runs=0
total=0

# untried NAME WHY: a combination that could not be compiled at all.
untried() {
    if grep -qxF -- "$1" "$here/compiler/expect-runs" 2> /dev/null; then
        echo "FAIL  $1: not tried, $2"; fail=1
    else
        echo "skip  $1: $2"
    fi
}

for m in 32 64; do
    printf 'int main(void){return 0;}\n' > probe.c
    can_build=1
    gcc -m$m -o probe probe.c 2> /dev/null || can_build=0

    for opts in "-O0" "-O1" "-O2" "-Os" "-O2 -fPIC" \
                "-O2 -fno-pie -fno-asynchronous-unwind-tables" \
                "-O2 -fno-pie -msse2 -mfpmath=sse" \
                "-O2 -fcf-protection=full" "-O2 -fcf-protection=none"; do
        name="-m$m $opts"
        if [ "$can_build" -eq 0 ]; then
            untried "$name" "the host's gcc cannot build a -m$m program"
            continue
        fi
        total=$((total + 1))
        verdict=
        # Code that is not position-independent is linked as a program
        # that is not.
        pie=-no-pie
        case "$opts" in *-fPIC*) pie=-pie ;; esac
        # $opts is a list of options: split it.
        # shellcheck disable=SC2086
        if ! gcc -m$m $opts -S -o t.s "$here/compiler/prog.c" 2> /dev/null ||
           ! gcc -m$m $opts $pie -o ref "$here/compiler/prog.c" 2> /dev/null; then
            untried "$name" "the host's gcc does not take these options"
            total=$((total - 1))
            continue
        fi
        ./ref > ref.out 2>&1
        rm -f t.o prog
        if ! "$AS" --$m -o t.o t.s 2> err; then
            verdict="AS    $(head -1 err | sed 's/^as: error: [^:]*:[0-9]*: //' | cut -c1-60)"
        else
            if ! gcc -m$m $pie -o prog t.o 2> lerr; then
                verdict="LINK  $(grep -v '^$' lerr | head -1 | cut -c1-60)"
            else
                # (Through a shell of its own, so that the report of a
                # program killed by a signal is that shell's and goes
                # nowhere.)
                timeout 20 sh -c './prog > out 2>&1' > /dev/null 2>&1
                if cmp -s out ref.out; then
                    verdict=RUNS
                    runs=$((runs + 1))
                else
                    verdict="WRONG"
                fi
            fi
        fi
        if grep -qxF -- "$name" "$here/compiler/expect-runs" 2> /dev/null; then
            case "$verdict" in
            RUNS) echo "ok    $name" ;;
            *) echo "FAIL  $name: $verdict"; fail=1 ;;
            esac
        else
            echo "      $name: $verdict"
        fi
    done
done

echo "compiler output: $runs of $total combinations run"
exit "$fail"
