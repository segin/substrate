#!/bin/sh
# Small things of how a source is read: labels run together, names in
# quotes, character constants, a NUL byte, and the conditionals.
#
# - `q1:q2: .long 4` and `a:b:nop` were refused: only the first of the
#   labels run together was taken for one.
# - `"x.y":` defines a name written in quotes, and `jmp "x.y"` uses it.
# - `'a` is a character constant with or without the quote that closes
#   it; without it `.byte 'a, 'a+1` was refused, and a `;` or `#` that
#   was the character began a statement or a comment.
# - A NUL byte ended the source where it stood, with exit status 0.
# - `.if` of what is not a number -- a label, a name not defined -- was
#   compared as text or counted as true; `.ifeqs` and `.ifnes` were not
#   known; a `.macro` in a branch not taken was defined; and what was
#   wrong with a conditional was "conditional assembly stage failed".
#
# The bytes are GNU as 2.46's.  In a case, `|` is a new line.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

# Each case: the modes, the bytes of .text (or `refused`), the source.
cat > cases <<'EOF'
32 64	0400000000000000	q1:q2: .long 4|.long q2-q1
32 64	9000	a: b: c: nop|.byte c-a
32 64	90	a:b:nop
32 64	90ebfdebfb	1: 2: nop|jmp 1b|jmp 2b
32 64	9001	"x.y": nop|.byte . - "x.y"
32 64	90	.globl "a b"|"a b": nop
32 64	9001	"a b": nop|.byte . - "a b"
32 64	05000000	.set "k k", 5|.long "k k"
32 64	909004000000	"a b": nop|nop|.long "a b" - . + 6
32 64	9001000000	1: "a b": nop|.long "a b"-1b+1
32 64	05000000	.Lq = 5|.long .Lq
32 64	0500000006000000	.Lq=5|x = .Lq+1|.long .Lq, x
32 64	900000000090	nop|. = . + 4|nop
32 64	900000000090	nop|.=.+4|nop
32 64	90000090	nop|.org .+2|nop
32 64	909000000090	nop|1: nop|.org 1b+4|nop
32 64	f30fa7c8	xcrypt-ecb
32 64	0300000001000000	.long 7%4, 7 % 3
32 64	b061	movb $'a',%al
32 64	b061	movb $'a,%al
32 64	6162	.byte 'a, 'a+1
32 64	6162	.byte 'a', 'b'
32 64	b862000000	movl $'a'+1, %eax
32 64	3b02	.byte ';, 2
32 64	2303	.byte '#, 3
32 64	2704	.byte '', 4
32 64	2705	.byte '\'', 5
32 64	0a095c	.byte '\n, '\t, '\\
32 64	refused	.long 'ab
32 64	refused	.byte 1, \|2, 3
32 64	refused	.if undefined_symbol|nop|.endif|ret
32 64	refused	lab: nop|.if lab|nop|.endif|ret
32 64	refused	.if lab2|nop|.endif|lab2: ret
32 64	c3	.if 0|.if undefined_thing|nop|.endif|.endif|ret
32 64	90	.if 1 == 1|nop|.endif
32 64	90	.if 2 > 1 && 1|nop|.endif
32 64	90	.set k, 1|.ifdef k|nop|.endif
32 64	f4	.ifdef nope|nop|.else|hlt|.endif
32 64	90	.ifndef nope|nop|.endif
32 64	90c3ccfcfd	.ifeq 0|nop|.endif|.ifne 0|hlt|.endif|.ifgt 1|ret|.endif|.iflt -1|int3|.endif|.ifge 0|cld|.endif|.ifle 0|std|.endif
32 64	f4	.if 0|nop|.elseif 1|hlt|.elseif 1|ret|.else|int3|.endif
32 64	90c3	.ifc a,a|nop|.endif|ret
32 64	c3	.ifc a,b|nop|.endif|ret
32 64	90c3	.ifnc a,b|nop|.endif|ret
32 64	90c3	.ifc "a b","a b"|nop|.endif|ret
32 64	90c3	.ifc a b, a b|nop|.endif|ret
32 64	c3	.ifb x|nop|.endif|ret
32 64	90c3	.ifnb x|nop|.endif|ret
32 64	c3	.ifnb|nop|.endif|ret
32 64	90c3	.ifeqs "a","a"|nop|.endif|ret
32 64	c3	.ifeqs "a","b"|nop|.endif|ret
32 64	90c3	.ifnes "a","b"|nop|.endif|ret
32 64	90f4c3	.macro m a|.ifc \a,x|nop|.else|hlt|.endif|.endm|m x|m y|ret
32 64	90f4c3	.macro m a|.ifb \a|nop|.else|hlt|.endif|.endm|m|m y|ret
32 64	90c3	.macro m a, b|.ifnc \a,\b|nop|.endif|.endm|m x, y|m x, x|ret
32 64	f4c3	.if 1|.macro mm|hlt|.endm|.endif|mm|ret
32 64	refused	.if 0|.macro mm|hlt|.endm|.endif|mm|ret
32 64	90	.if 0|.section .foo|.endif|nop
32 64	refused	.endif|nop
32 64	refused	.else|nop
32 64	refused	.elseif 1|nop|.endif
32 64	refused	.if 1|nop
32 64	refused	.if 1|.if 0|nop|.endif|ret
32 64	refused	.if 1|nop|.else|hlt|.else|ret|.endif
EOF

while IFS='	' read -r modes want src; do
    for mode in $modes; do
        cases=$((cases + 1))
        { echo .text; printf '%s\n' "$src" | tr '|' '\n'; } > t.s
        rm -f t.o
        if "$AS" "-$mode" -o t.o t.s > out 2> err; then
            got=$(objcopy -O binary --only-section=.text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
        else
            got=refused
        fi
        [ "$got" = "$want" ] || { echo "FAIL $mode [$src]: $got, and GNU as: $want $(head -1 err)"; fail=1; }
    done
done < cases

# What is wrong with a conditional is said, with its line -- and, for
# one never closed, the line it began on.
said() {
    cases=$((cases + 1))
    printf "$3" > w.s
    if "$AS" -32 -o w.o w.s > out 2> err; then
        echo "FAIL $1: assembled"; fail=1; return
    fi
    grep -q "$2" err || { echo "FAIL $1: no '$2' in: $(head -1 err)"; fail=1; }
}
said ".endif alone"           'w\.s:3:' '.text\nnop\n.endif\n'
said ".else twice"            'w\.s:6:' '.text\nnop\n.if 1\nnop\n.else\n.else\n.endif\n'
said ".if never closed"       'w\.s:2:' '.text\n.if 1\n.ifdef x\nnop\n.endif\nnop\n'
said ".if of a name undefined" 'w\.s:2:' '.text\n.if zzz\nnop\n.endif\n'

# A NUL byte is no character of a source.  (GNU as passes over it.)
if ! "$AS" --version 2>/dev/null | grep -qi 'GNU assembler'; then
    cases=$((cases + 1))
    printf 'nop\n\000bad\nnop\n' > nul.s
    rm -f nul.o
    if "$AS" -32 -o nul.o nul.s > out 2> err; then
        echo "FAIL a NUL byte: assembled"; fail=1
    else
        grep -q 'nul\.s:2:' err || { echo "FAIL a NUL byte: $(head -1 err)"; fail=1; }
    fi
    [ ! -s nul.o ] || { echo "FAIL a NUL byte: an object"; fail=1; }
fi

[ "$fail" -eq 0 ] && echo "ok: front-end forms ($cases cases)"
exit "$fail"
