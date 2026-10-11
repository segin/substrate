#!/bin/sh
# The differential test: every line of the two instruction corpora is
# assembled by this assembler and by GNU as, and the two must agree on
# its bytes and relocations but for the lines listed as known to differ.
#
#   corpus/x86_32_lines.txt      11,706 lines of 32-bit code
#   corpus/x86_64_lines.txt      18,072 lines of 64-bit code
#   corpus/*.baseline            the lines of each that differ today
#
# The test fails when a line outside a baseline differs -- something
# that was right is now wrong -- and when a line inside one no longer
# does, so that the commit which mends a line takes it out:
#
#   AS=... python3 gnu-diff.py --32 --baseline corpus/x86_32_lines.baseline \
#       --write-baseline corpus/x86_32_lines.txt
#
# and the diff of the baseline is the record of what the commit did.
#
# Where GNU as is not installed there is nothing to compare with, and
# the test says that it was skipped and passes.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
case $AS in /*) ;; *) AS=$(pwd)/$AS ;; esac
export AS
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if ! command -v python3 > /dev/null 2>&1; then
    echo "skipped: python3 is not installed"
    exit 0
fi

run() {
    python3 "$here/gnu-diff.py" "$1" --baseline "$here/corpus/$2.baseline" "$here/corpus/$2.txt" > "$work/$2.out" 2>&1
    echo $? > "$work/$2.rc"
}

# The two at once: each is most of a minute of small processes.
run --32 x86_32_lines &
run --64 x86_64_lines &
wait

fail=0
skipped=0
for c in x86_32_lines x86_64_lines; do
    rc=$(cat "$work/$c.rc" 2>/dev/null || echo 1)
    cat "$work/$c.out"
    case $rc in
    0) ;;
    77) skipped=1 ;;
    *) fail=1 ;;
    esac
done
if [ "$skipped" = 1 ] && [ "$fail" = 0 ]; then
    echo "ok: differential test skipped"
    exit 0
fi
[ "$fail" -eq 0 ] && echo "ok: this assembler and GNU as differ only where the baselines say"
exit "$fail"
