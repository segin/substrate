#!/bin/sh
# What a failed assembly says, and what it leaves.
#
# - The first statement in error ended the run: a source with three
#   mistakes was assembled three times to find them.  Each is reported.
# - An error of a line is `file:line: Error: text`, the form an editor
#   or a build looks for; it was `as: error: file:line: text`.
# - A failed run left the object of the run before it where it was, to
#   be linked as if it were this source's.  It is removed.
# - And it leaves nothing else: nothing in $TMPDIR, nothing beside the
#   output.
#
# GNU as passes this test but for the last part: it creates its output
# before it reads its source, and a signal that ends it leaves the
# output it had begun.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
mkdir tmp src
TMPDIR=$work/tmp
export TMPDIR
cd src || exit 1

fail=0
cases=0

# Three statements in error, of three kinds, and good ones between.
cases=$((cases + 1))
printf '\tfooop %%eax,%%ebx\n\tnop\n\tmov %%badreg,%%eax\n\tret\n\tbarrr\n' > three.s
if "$AS" -32 -o three.o three.s > out 2> err; then
    echo "FAIL three errors: assembled"; fail=1
else
    for line in 1 3 5; do
        grep -q "three\.s:$line: Error: ." err || { echo "FAIL three errors: line $line is not reported"; fail=1; }
    done
    grep -q 'three\.s:[24]: ' err && { echo "FAIL three errors: a good line is reported"; fail=1; }
    [ ! -e three.o ] || { echo "FAIL three errors: an object was written"; fail=1; }
fi

# Each has the form, and says something.
form() {
    cases=$((cases + 1))
    printf "$2" > f.s
    if "$AS" -32 -o f.o f.s > out 2> err; then
        echo "FAIL $1: assembled"; fail=1; return
    fi
    # (GNU as gives two of these no line, and a line of context after
    # some: "f.s: Error: ...", "f.s:2: Info: ...".)
    bad=$(grep -v 'Assembler messages:$' err | grep -v ': Info: ' |
          grep -v '^[^ ]*f\.s:\([0-9][0-9]*:\)* Error: [^ ]' | head -1)
    [ -z "$bad" ] || { echo "FAIL $1: not file:line: Error: text: $bad"; fail=1; }
    [ -s err ] || { echo "FAIL $1: nothing said"; fail=1; }
}
form "an unknown instruction"      '\tnop\n\tfrobnicate\n'
form "an operand that is not one"  '\tmov %%nope, %%eax\n'
form "an unknown directive"        '\t.frobnicate 1\n'
form "a branch to no label"        '\tloop 1b\n'
form "a numeric label not there"   '\tjmp 1f\n\tnop\n'
form "a conditional never closed"  '\t.if 1\n\tnop\n'
form "a macro never closed"        '\t.macro m\n\tnop\n'
form "a scale that is none"        '\tmovl (%%eax,%%ebx,3), %%ecx\n'
form "an immediate that does not fit" '\tint $256\n'

# The object of a run before is not left after one that fails.
cases=$((cases + 1))
printf '\tnop\n' > good.s
printf '\tnop\n\tbadone\n' > bad.s
"$AS" -32 -o out.o good.s 2> err || { echo "FAIL: a good source: $(head -1 err)"; fail=1; }
[ -s out.o ] || { echo "FAIL: a good source wrote no object"; fail=1; }
"$AS" -32 -o out.o bad.s > out 2> err && { echo "FAIL: a bad source assembled"; fail=1; }
[ ! -e out.o ] || { echo "FAIL: the old object is still there after a failed run"; fail=1; }

# And nothing else is left, by a run that fails or by one that does not.
cases=$((cases + 1))
rm -f out err f.o three.o
"$AS" -32 -o good.o good.s 2> /dev/null
"$AS" -32 -o bad.o bad.s 2> /dev/null
left=$(ls | grep -v '\.s$' | grep -v '^good\.o$' | tr '\n' ' ')
[ -z "$left" ] || { echo "FAIL: left beside the output: $left"; fail=1; }
[ -z "$(ls "$TMPDIR")" ] || { echo "FAIL: left in TMPDIR: $(ls "$TMPDIR" | tr '\n' ' ')"; fail=1; }

# Ended by a signal, it dies of the signal and leaves nothing.  (The
# source is a pipe nothing is written to, so that it is still reading
# when the signal comes.  Not SIGINT: a shell has it ignored in what it
# runs in the background, and what is ignored stays ignored.)
for sig in TERM HUP; do
    cases=$((cases + 1))
    rm -f pipe sig.o
    mkfifo pipe
    exec 9<> pipe
    "$AS" --32 -o sig.o pipe > /dev/null 2>&1 &
    pid=$!
    # Until it has the pipe open, and so has begun.
    n=0
    while [ "$n" -lt 200 ] && ! ls -l "/proc/$pid/fd" 2> /dev/null | grep -q 'src/pipe$'; do n=$((n + 1)); done
    kill "-$sig" "$pid" 2> /dev/null
    { wait "$pid"; } 2> /dev/null
    status=$?
    exec 9>&-
    rm -f pipe
    [ "$status" -gt 128 ] || { echo "FAIL SIG$sig: exit status $status, not death by the signal"; fail=1; }
    [ ! -e sig.o ] || { echo "FAIL SIG$sig: an object was left"; fail=1; }
    left=$(ls -A | grep -v '\.s$' | grep -v '^good\.o$' | tr '\n' ' ')
    [ -z "$left" ] || { echo "FAIL SIG$sig: left beside the output: $left"; fail=1; }
    [ -z "$(ls -A "$TMPDIR")" ] || { echo "FAIL SIG$sig: left in TMPDIR"; fail=1; }
done

[ "$fail" -eq 0 ] && echo "ok: error reports ($cases cases)"
exit "$fail"
