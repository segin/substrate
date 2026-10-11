#!/bin/sh
# The processors -march= names (AS-T-143).
#
# Eleven names were taken in 32-bit mode and none of GNU as's in 64-bit:
# -march=k8, core2, znver3 were refused, and so a build that passes one
# to the assembler did not build.  The names are GNU as's, each with the
# extensions that may follow it; a name that is none is refused; and a
# processor without 64-bit mode is refused in that mode.
#
# GNU as passes this test.  (This assembler's own names -- x86-64-v2 and
# the like, which GNU as has not -- are in test_cli_driver.sh.)
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0
printf '\tnop\n' > n.s

# taken MODE NAME / refused MODE NAME
taken() {
    cases=$((cases + 1))
    "$AS" "--$1" "-march=$2" -o t.o n.s 2> err || { echo "FAIL -$1 -march=$2: $(tail -1 err)"; fail=1; }
}
refused() {
    cases=$((cases + 1))
    if "$AS" "--$1" "-march=$2" -o t.o n.s 2> err; then
        echo "FAIL -$1 -march=$2 is taken"; fail=1
    else
        [ -s err ] || { echo "FAIL -$1 -march=$2: refused, and nothing said"; fail=1; }
    fi
}

# With 64-bit mode, and so in either.
for cpu in generic64 nocona core2 corei7 opteron k8 amdfam10 bdver1 bdver2 bdver3 bdver4 \
           znver1 znver2 znver3 znver4 znver5 btver1 btver2; do
    taken 32 "$cpu"
    taken 64 "$cpu"
done
# Without.
for cpu in generic32 i386 i486 i586 pentium i686 pentiumpro pentiumii pentiumiii pentium4 \
           prescott core k6 k6_2 athlon; do
    taken 32 "$cpu"
    refused 64 "$cpu"
done
# Names that are not GNU as's: those are gcc's.
for cpu in haswell skylake nosuchcpu K8 ''; do
    refused 32 "$cpu"
    refused 64 "$cpu"
done
# Extensions.
for cpu in k8+avx2 k8+noavx2 znver3+avx512f+nosse4a i686+sse2 core2+sse4.1 k8+387; do
    taken 32 "$cpu"
done
for cpu in k8+nosuch k8+ k8+avx2+ nosuch+avx2; do
    refused 64 "$cpu"
done

[ "$fail" -eq 0 ] && echo "ok: -march names ($cases cases)"
exit "$fail"
