#!/bin/sh
# .include reads the file it names (AS-T-123, AS-T-124, AS-T-125).
#
# It read nothing.  The lexer asked whether a line was an .include after
# moving the line's tokens away, so the answer was always no: the file
# was not opened, existed or not, and the directive was passed on and
# ignored.  A source that kept its constants in an included file
# assembled without a word, with every one of them undefined.
#
# The bytes are GNU as's.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

text_of() { objcopy -O binary -j .text "$1" /dev/stdout | od -An -v -tx1 | tr -d ' \n'; }

# is WHAT BYTES ARGS...: assembling with ARGS gives BYTES
is() {
    what=$1; want=$2; shift 2
    rm -f t.o
    if ! "$AS" --32 "$@" -o t.o 2> err; then
        echo "FAIL $what: $(head -1 err | sed 's/^as: error: //')"; fail=1; return
    fi
    got=$(text_of t.o)
    [ "$got" = "$want" ] || { echo "FAIL $what: $got, not $want"; fail=1; }
}

# refused WHAT WORD ARGS...: assembling fails, and says WORD
refused() {
    what=$1; word=$2; shift 2
    rm -f t.o
    if "$AS" --32 "$@" -o t.o 2> err; then
        echo "FAIL $what: assembled"; fail=1; return
    fi
    grep -q -- "$word" err || { echo "FAIL $what: said '$(head -1 err)'"; fail=1; }
}

mkdir inc sub
printf '\tnop\n\thlt\n' > here.inc
printf '.equ K, 7\n\tnop\n' > inc/defs.inc
printf '\tnop\n.include "inner.inc"\n' > sub/outer.inc
printf '\thlt\n' > sub/inner.inc

# In the current directory; its lines stand where the directive was.
printf '\t.text\n\tcli\n.include "here.inc"\n\tcli\n' > a.s
is "the current directory" fa90f4fa a.s

# In a -I directory, and what it defines is defined.
printf '\t.text\n.include "defs.inc"\n\tmov $K, %%eax\n' > b.s
is "a -I directory" 90b807000000 -I inc b.s
refused "without the -I" 'defs.inc' b.s

# A path with a directory in it, and a file that includes another: the
# second is looked for as the first was, not beside the first.
printf '\t.text\n.include "sub/outer.inc"\n\tret\n' > c.s
is "an include in an include" 90f4c3 -I sub c.s
refused "the inner one not on the path" 'inner.inc' c.s

# The -I directories in the order given, ahead of the current directory.
mkdir first second
printf '\tcli\n' > first/pick.inc
printf '\tsti\n' > second/pick.inc
printf '\thlt\n' > pick.inc
printf '\t.text\n.include "pick.inc"\n' > d.s
is "the first -I"            fa -I first -I second d.s
is "the other way about"     fb -I second -I first d.s
is "none: the current one"   f4 d.s

# An absolute name is itself and is not searched for.
printf '\t.text\n.include "%s/here.inc"\n' "$work" > e.s
is "an absolute name" 90f4 e.s

# Twice is twice.
printf '\t.text\n.include "here.inc"\n.include "here.inc"\n' > f.s
is "twice" 90f490f4 f.s

# A file that is not there, and one that includes itself.
printf '\t.text\n.include "nosuch.inc"\n\tret\n' > g.s
refused "a file that is not there" 'nosuch.inc' g.s
printf '.include "h.s"\n' > h.s
refused "a file that includes itself" 'h.s' h.s

# The temporary directory is not searched: the driver's copy of the
# source is there, and so is whatever anyone else has left.
# (The copy goes in /tmp whatever TMPDIR is, so that is where to plant.)
planted=$(mktemp /tmp/as-include-XXXXXX)
printf '\tint3\n' > "$planted"
printf '\t.text\n.include "%s"\n' "$(basename "$planted")" > i.s
refused "a file in the temporary directory" "$(basename "$planted")" i.s
rm -f "$planted"

[ "$fail" -eq 0 ] && echo "ok: .include"
exit "$fail"
