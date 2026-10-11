#!/bin/sh
# A message names the file and the line its reader wrote.
#
# Macros, repeats and conditionals were expanded through files in /tmp,
# four of them for a source with none, and a statement had the name and
# the line of the last: every message said "/tmp/ascond_dlnR1j:5".  With
# the standard input or several sources the name was another file in
# /tmp, and with a line of a macro's expansion the number was of no line
# the user had.
#
# The expansion is in memory, and each line of it knows where it came
# from.  GNU as passes this test: it asks for the file and the line, and
# not for how the rest is worded.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# at WHAT WHERE [ARGS...]: the assembly fails, and says WHERE.
at() {
    what=$1 where=$2
    shift 2
    cases=$((cases + 1))
    rm -f t.o
    if "$AS" -32 -o t.o "$@" > out 2> err; then
        echo "FAIL $what: assembled"; fail=1; return
    fi
    grep -q "$where" err || { echo "FAIL $what: no '$where' in: $(head -2 err | tr '\n' ' ')"; fail=1; }
    if grep -q '/tmp/as\|ascond\|asin_' err; then
        echo "FAIL $what: names a file of the assembler's own: $(head -1 err)"; fail=1
    fi
}

printf '\t.text\n\tnop\n\tbadone %%eax\n' > plain.s
at "a plain source" 'plain\.s:3:' plain.s

printf '\t.text\n\n\n\t.if 0\n\tnop\n\tnop\n\t.endif\n\tbadone\n' > cond.s
at "after a conditional" 'cond\.s:8:' cond.s

printf '\t.text\n\t.macro m a\n\tmovl $\\a, %%eax\n\tnop\n\t.endm\n\tnop\n\tm 1\n\tm 2\n\tbadone\n' > mac.s
at "after macros are expanded" 'mac\.s:9:' mac.s

printf '\t.text\n\t.macro m a\n\tnop\n\tbadone \\a\n\t.endm\n\tnop\n\tnop\n\tm 1\n' > inmac.s
at "inside a macro: where it was called" 'inmac\.s:8:' inmac.s

printf '\t.text\n\t.rept 3\n\tnop\n\tnop\n\t.endr\n\tbadone\n' > rept.s
at "after a repeat" 'rept\.s:6:' rept.s

printf '\t.text\n\t.rept 2\n\tnop\n\tbadone\n\t.endr\n' > inrept.s
at "inside a repeat" 'inrept\.s:4:' inrept.s

printf '\t.text\n\tnop; nop\n\tnop; badone\n' > semi.s
at "the second statement of a line" 'semi\.s:3:' semi.s

printf '\t.text\n\tnop\n' > first.s
printf '\tret\n\tbadone\n' > second.s
at "the second of two sources" 'second\.s:2:' first.s second.s

printf '\t.text\n\t.include "second.s"\n' > inc.s
at "an included file" 'second\.s:2:' inc.s

# The lines a C preprocessor leaves say where what follows came from.
printf '\t.text\n\tnop\n# 100 "orig.c"\n\tnop\n\tbadone\n' > marked.s
at "after a # N \"file\" line" 'orig\.c:101:' marked.s

# And a .S file is put through one: an error after an #include is on the
# line of the .S file it is on.  (GNU as does not run the preprocessor.)
if ! "$AS" --version 2>/dev/null | grep -qi 'GNU assembler'; then
    printf '#define M nop\n\n\n\n' > hh.h
    printf '#include "hh.h"\n\t.text\n\tM\n\tnop\n\tbadone\n' > pre.S
    at "a .S file with an #include" 'pre\.S:5:' pre.S
fi

# The standard input has a name, and not that of a file in /tmp.
cases=$((cases + 1))
if printf '\t.text\n\tnop\n\tbadone\n' | "$AS" -32 -o in.o - > out 2> err; then
    echo "FAIL the standard input: assembled"; fail=1
else
    grep -q 'standard input}:3:' err || { echo "FAIL the standard input: $(head -1 err)"; fail=1; }
fi

# A warning is placed as an error is.
cases=$((cases + 1))
printf '\t.data\n\t.macro w\n\t.byte 300\n\t.endm\n\t.byte 1\n\t.if 0\n\t.byte 2\n\t.endif\n\tw\n\t.byte 256\n' > warn.s
if "$AS" -32 -o warn.o warn.s > out 2> err; then
    grep -q 'warn\.s:9:' err || { echo "FAIL a warning in a macro: $(tr '\n' ' ' < err)"; fail=1; }
    grep -q 'warn\.s:10:' err || { echo "FAIL a warning after it: $(tr '\n' ' ' < err)"; fail=1; }
else
    echo "FAIL warnings: $(head -1 err)"; fail=1
fi

# And what is assembled is what it was: a macro and a repeat, expanded.
cases=$((cases + 1))
printf '\t.text\n\t.macro m a\n\t.byte \\a\n\t.byte \\a + 1\n\t.endm\n\t.rept 2\n\tm 1\n\t.endr\n\t.if 1\n\t.byte 9\n\t.else\n\t.byte 8\n\t.endif\n' > ok.s
if "$AS" -32 -o ok.o ok.s 2> err; then
    got=$(objcopy -O binary --only-section=.text ok.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ "$got" = 0102010209 ] || { echo "FAIL the expansion: .text is $got"; fail=1; }
else
    echo "FAIL the expansion: $(head -1 err)"; fail=1
fi

# This assembler's own: it makes no file in /tmp on the way.
if ! "$AS" --version 2>/dev/null | grep -qi 'GNU assembler'; then
    cases=$((cases + 1))
    if grep -q 'mkstemp *(\|tmpfile *(\|\[\] = "/tmp/' "$top/usr.bin/as/as.c"; then
        echo "FAIL: as.c makes a temporary file"; fail=1
    fi
fi

[ "$fail" -eq 0 ] && echo "ok: source positions ($cases cases)"
exit "$fail"
