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
        echo "FAIL $what: $(head -1 err)"; fail=1; return
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

# The current directory first, and then the -I directories in the order
# given: GNU as's order.  (The -I directories were looked in first.)
mkdir first second
printf '\tcli\n' > first/pick.inc
printf '\tsti\n' > second/pick.inc
printf '\thlt\n' > pick.inc
printf '\t.text\n.include "pick.inc"\n' > d.s
is "the current directory, before any -I" f4 -I first -I second d.s
is "none: the current one"   f4 d.s
printf '\tcli\n' > first/only.inc
printf '\tsti\n' > second/only.inc
printf '\t.text\n.include "only.inc"\n' > d2.s
is "the first -I"            fa -I first -I second d2.s
is "the other way about"     fb -I second -I first d2.s

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

# An included file is expanded as the file that includes it is, in its
# place: its macros are defined for what follows, the macros defined
# before it are its to call, and its conditionals are followed.  (It was
# read after all of that was done, as text to be tokenized.)
printf '.macro mm\n\thlt\n.endm\n.set K, 3\n.if K == 3\n\tnop\n.else\n\tint3\n.endif\n1:\n' > m.inc
printf '\t.text\n.include "m.inc"\n\tmm\n\tjmp 1b\n.ifdef K\n\t.byte K\n.endif\n' > j.s
is "a macro, a symbol and a label of an included file" 90f4ebfd03 j.s
printf '\tpm\n\tpm\n' > u.inc
printf '\t.text\n.macro pm\n\tcld\n.endm\n.include "u.inc"\n' > k.s
is "a macro of the including file, called in the included" fcfc k.s

# A numeric label is the nearest in the order assembled, out of an
# included file and into one.
printf '\tjmp 1b\n1:\thlt\n' > back.inc
printf '\t.text\n1:\tnop\n.include "back.inc"\n\tjmp 1b\n' > l.s
is "1b across the boundary of an included file" 90ebfdf4ebfd l.s

# In a branch not taken the file is not read, and need not be there.
printf '\t.text\n.if 0\n.include "nosuch.inc"\n.endif\n\tnop\n' > m.s
is "an .include in a branch not taken" 90 m.s

# After a label on its line.
printf '\tnop\n\tnop\n' > two.inc
printf '\t.text\nlab: .include "two.inc"\n\t.byte . - lab\n' > n.s
is "an .include after a label" 909002 n.s

# What is wrong is said with the line of the file it is in: of the
# including file for a file that is not there, of the included one for
# what is in it.
refused "a file not there, at the including line" 'g\.s:2:' g.s
printf '\tnop\n\tbadone\n' > bad.inc
printf '\t.text\n\tnop\n.include "bad.inc"\n' > o.s
refused "an error in an included file, at its line" 'bad\.inc:2:' o.s

# .incbin "file", skip, count.
printf 'ABCDEFGHIJ' > bin.dat
ib() {
    printf '\t.text\n\t.byte 1\n\t.incbin %s\n\t.byte 2\n' "$3" > ib.s
    if [ "$2" = refused ]; then
        refused ".incbin $1" 'ib\.s' ib.s
    else
        is ".incbin $1" "$2" ib.s
    fi
}
ib "the whole file"          014142434445464748494a02 '"bin.dat"'
ib "from a skip"             01434445464748494a02     '"bin.dat", 2'
ib "a skip and a count"      0143444502               '"bin.dat", 2, 3'
ib "to the last byte"        01434445464748494a02     '"bin.dat", 2, 8'
ib "a skip of all of it"     0102                     '"bin.dat", 10'
ib "an empty skip"           0141424302               '"bin.dat",,3'
ib "a count of 0: the rest"  01434445464748494a02     '"bin.dat", 2, 0'
ib "a skip past the end"     refused                  '"bin.dat", 11'
ib "a count past the end"    refused                  '"bin.dat", 2, 9'
ib "a file that is not there" refused                 '"nosuch.dat"'

[ "$fail" -eq 0 ] && echo "ok: .include"
exit "$fail"
