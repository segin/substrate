#!/bin/sh
# Macros and repeats, as GNU as 2.46 has them.
#
# - A macro defined twice replaced the first without a word; .purgem
#   was not known.
# - A macro that calls itself and ends with `.if` or `.exitm` did not
#   end: macros were expanded before any conditional was looked at.
# - A parameter :req was required only if it was the first, and :vararg
#   took one argument and dropped the rest.
# - \@ began at 1.
# - A .macro in a macro's body ended the outer body at the first .endm.
# - A .macro, .rept or .irp never closed was assembled as far as it
#   went.
# - `.ascii "a" "b"` stored a space between the two.
# - A repeat of two thousand million was begun.
#
# The bytes are GNU's.  In a case, `|` is a new line.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0

cat > cases <<'EOF'
refused	.macro m|nop|.endm|.macro m|hlt|.endm|m
f4	.macro m|nop|.endm|.purgem m|.macro m|hlt|.endm|m
refused	.macro m|nop|.endm|.purgem m|m
90	.purgem nosuch|nop
030201	.macro m n|.if \n|.byte \n|m (\n - 1)|.endif|.endm|m 3
030201	.macro m n|.if \n == 0|.exitm|.endif|.byte \n|m (\n - 1)|.endm|m 3
90c3	.macro m|nop|.exitm|hlt|.endm|m|ret
05	.macro m a:req|.byte \a|.endm|m 5
refused	.macro m a:req|.byte \a|.endm|m
refused	.macro m a, b:req|.byte \a, \b|.endm|m 1
01020304	.macro m a, b:vararg|.byte \a|.byte \b|.endm|m 1, 2, 3, 4
010203	.macro m a:vararg|.byte \a|.endm|m 1, 2, 3
09	.macro m a:vararg|.byte 9|.endm|m
0708	.macro m a=7|.byte \a|.endm|m|m 8
010301020405	.macro m a, b=3|.byte \a, \b|.endm|m 1|m 1, 2|m b=5, a=4
612062	.macro m x|.ascii "\x"|.endm|m "a b"
6162	.macro m x|.ascii "\x"|.endm|m ab
b805000000	.macro m x|movl \x, %eax|.endm|m "$5"
010203	.macro m x, y|.byte \x|.byte \y|.endm|m "1, 2", 3
000102	.macro m|.byte \@|.endm|m|m|m
000100020302	.macro a|.byte \@|.endm|.macro b|.byte \@|a|.byte \@|.endm|b|b
07	.macro outer|.macro inner|.byte 7|.endm|.endm|outer|inner
0102	.macro outer n|.macro inner\n|.byte \n|.endm|.endm|outer 1|outer 2|inner1|inner2
0102	.macro m a b|.byte \a, \b|.endm|m 1 2
0c	.macro m a, b|.byte \a\()\b|.endm|m 1, 2
010201	.macro m a|l\a: .byte \a|.endm|m 1|m 2|.byte l2 - l1
refused	.macro m a a|.endm
refused	.macro|nop|.endm
refused	.macro m|nop
refused	.rept 2|nop
refused	.irp x, 1, 2|.byte \x
refused	.irpc x, 12|.byte \x
01	.irp x|.byte 1\x|.endr
6162	.irp x, a, b|.ascii "\x"|.endr
6162	.irpc x, ab|.ascii "\x"|.endr
90	.rept 0|hlt|.endr|nop
9090c39090c3	.rept 2|.rept 2|nop|.endr|ret|.endr
9090	.macro m|.rept 2|nop|.endr|.endm|m
6162	.ascii "a" "b"
6162	.ascii "a""b"
616200	.string "a" "b"
616200	.asciz "a" "b"
612262	.ascii "a\"" "b"
616263	.ascii "a" , "b" "c"
6162006300	.string "a"  "b", "c"
610062000000	.string16 "a" "b"
90	.noaltmacro|nop
EOF

while IFS='	' read -r want src; do
    for mode in 32 64; do
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

# What is wrong is said, and where -- and where the thing began.
said() {
    cases=$((cases + 1))
    printf "$3" > w.s
    if "$AS" -32 -o w.o w.s > out 2> err; then
        echo "FAIL $1: assembled"; fail=1; return
    fi
    grep -q "$2" err || { echo "FAIL $1: no '$2' in: $(head -1 err)"; fail=1; }
}
said "a .macro never closed" 'w\.s:2:'  '.text\n.macro m\nnop\n'
said "a .rept never closed"  'w\.s'     '.text\nnop\n.rept 2\nnop\n'
said "an .irp never closed"  'w\.s:2:'  '.text\n.irp x, 1\n.byte \\x\n'

# The rest is this assembler's own: the limits, and their wording.
if "$AS" --version 2>/dev/null | grep -i 'GNU assembler' | grep -qv '(Substrate)'; then
    [ "$fail" -eq 0 ] && echo "ok: macro forms ($cases cases; the limits are this assembler's own)"
    exit "$fail"
fi

said "a .rept never closed names where it began" 'w\.s:3:' '.text\nnop\n.rept 2\nnop\n'
said "a macro defined twice names the first" 'w\.s:2'  '.text\n.macro m\nnop\n.endm\nnop\n.macro m\nhlt\n.endm\n'
# A repeat of nothing is nothing, however many times.
cases=$((cases + 1))
printf '.text\n.rept 0x7fffffff\n.endr\nnop\n' > none.s
timeout 20 "$AS" -32 -o none.o none.s 2> err || { echo "FAIL .rept of an empty body: $(head -1 err)"; fail=1; }
said "a parameter required and not given"   'requires' '.text\n.macro m a, b:req\n.byte \\a\n.endm\nm 1\n'
said "a macro that calls itself without end" 'max-macro-depth' '.macro m\nm\n.endm\n.text\nm\n'
said "an .altmacro %expr of no known value" 'no value known here' '.macro m a\n.byte \\a\n.endm\n.data\n.altmacro\nm %%later\n.set later, 1\n'

# lim WHAT STATUS OPTION SOURCE: with the option, the exit status.
lim() {
    cases=$((cases + 1))
    printf "$4" > l.s
    "$AS" -32 "$3" -o l.o l.s > out 2> err
    rc=$?
    [ "$rc" -ne 0 ] && rc=1
    [ "$rc" = "$2" ] || { echo "FAIL $1: exit status $rc, not $2: $(head -1 err)"; fail=1; }
}
deep='.macro a n\n.if \\n\na (\\n-1)\n.endif\n.endm\n.text\na 10\nnop\n'
lim "--max-macro-depth below what is needed" 1 --max-macro-depth=5  "$deep"
lim "--max-macro-depth above it"             0 --max-macro-depth=20 "$deep"
lim "--max-expansion-bytes below"            1 --max-expansion-bytes=100    '.text\n.rept 100\nnop\n.endr\n'
lim "--max-expansion-bytes above"            0 --max-expansion-bytes=100000 '.text\n.rept 100\nnop\n.endr\n'
lim "a line of exactly --max-line-bytes"     0 --max-line-bytes=3 'nop\n'
lim "a line of one more"                     1 --max-line-bytes=2 'nop\n'
lim "a token of exactly --max-token-length"  0 --max-token-length=3 'nop\n'
lim "a token of one more"                    1 --max-token-length=2 'nop\n'
for bad in -1 0 abc 1x; do
    lim "--max-line-bytes=$bad is no limit"      1 "--max-line-bytes=$bad" 'nop\n'
    lim "--max-expansion-bytes=$bad is no limit" 1 "--max-expansion-bytes=$bad" 'nop\n'
done

# A repeat that asks for more than there is fails, and soon.
cases=$((cases + 2))
printf '.text\n.rept 0x7fffffff\nnop\n.endr\n' > big.s
if timeout 20 "$AS" -32 -o big.o big.s > out 2> err; then
    echo "FAIL .rept 0x7fffffff: assembled"; fail=1
else
    grep -q 'max-expansion-bytes' err || { echo "FAIL .rept 0x7fffffff: $(head -1 err)"; fail=1; }
fi
printf '.text\n.rept 200000\n.rept 200000\nnop\n.endr\n.endr\n' > nest.s
if timeout 20 "$AS" -32 -o nest.o nest.s > out 2> err; then
    echo "FAIL two nested .rept 200000: assembled"; fail=1
else
    grep -q 'max-expansion-bytes' err || { echo "FAIL two nested .rept 200000: $(head -1 err)"; fail=1; }
fi

[ "$fail" -eq 0 ] && echo "ok: macro forms ($cases cases)"
exit "$fail"
