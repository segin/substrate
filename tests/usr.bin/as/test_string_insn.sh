#!/bin/sh
# The string instructions (AS-T-054, AS-T-055).
#
# In 32-bit code `movsb`, `movsl`, `insb`, `outsl` and `rep movsb` -- the
# way they are nearly always written, with no operands -- were refused:
# only the two-operand forms of movs, ins and outs were known.  Written
# with operands they were wrong another way.  The one segment prefix a
# string instruction can have is its source's, and the first segment
# written was taken instead, so `movsb %fs:(%esi), %es:(%edi)` got an %es
# prefix and read from the wrong segment, and every destination written
# out in full, `%es:(%edi)`, got a prefix it does not have.  `movsw` with
# operands had its 66 twice.  `stosq` in 32-bit code was stosl.
#
# The bytes are GNU as's, the order of the prefixes among them: segment,
# operand size, rep.  Run by run-suite.sh, which sets $AS.
set -u

: "${AS:?run this through run-suite.sh, or set AS to a host assembler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
fail=0

# enc MODE INSTRUCTION BYTES
enc() {
    printf '\t.text\n\t%s\n' "$2" > t.s
    if ! "$AS" "--$1" -o t.o t.s 2> err; then
        echo "FAIL --$1 $2: $(head -1 err | sed 's/.*: //')"; fail=1; return
    fi
    got=$(objcopy -O binary -j .text t.o /dev/stdout | od -An -v -tx1 | tr -d ' \n')
    [ "$got" = "$3" ] || { echo "FAIL --$1 $2: $got, not $3"; fail=1; }
}

# refused MODE INSTRUCTION
refused() {
    printf '\t.text\n\t%s\n' "$2" > t.s
    if "$AS" "--$1" -o t.o t.s 2> /dev/null; then
        echo "FAIL --$1 $2: assembled"; fail=1
    fi
}

# With no operands, in both modes.
for m in 32 64; do
    enc $m movsb a4;    enc $m movsw 66a5;  enc $m movsl a5
    enc $m lodsb ac;    enc $m lodsw 66ad;  enc $m lodsl ad
    enc $m stosb aa;    enc $m stosw 66ab;  enc $m stosl ab
    enc $m cmpsb a6;    enc $m cmpsw 66a7;  enc $m cmpsl a7
    enc $m scasb ae;    enc $m scasw 66af;  enc $m scasl af
    enc $m insb 6c;     enc $m insw 666d;   enc $m insl 6d
    enc $m outsb 6e;    enc $m outsw 666f;  enc $m outsl 6f
    enc $m 'rep movsb'    f3a4
    enc $m 'rep movsl'    f3a5
    enc $m 'rep movsw'    66f3a5
    enc $m 'rep stosl'    f3ab
    enc $m 'rep stosw'    66f3ab
    enc $m 'repe cmpsb'   f3a6
    enc $m 'repne scasb'  f2ae
    enc $m 'rep insw'     66f36d
    enc $m 'rep outsb'    f36e
done
enc 64 movsq 48a5;  enc 64 lodsq 48ad;  enc 64 stosq 48ab
enc 64 cmpsq 48a7;  enc 64 scasq 48af;  enc 64 'rep stosq' f348ab
# The q forms are not 32-bit code's.
for i in movsq lodsq stosq cmpsq scasq 'rep stosq'; do refused 32 "$i"; done

# With operands: no prefix for the %es destination, nor for a %ds source.
enc 32 'movsb (%esi), %es:(%edi)'       a4
enc 32 'movsw (%esi), %es:(%edi)'       66a5
enc 32 'movsl (%esi), %es:(%edi)'       a5
enc 32 'movs (%esi), %es:(%edi)'        a5
enc 32 'movsw %ds:(%esi), %es:(%edi)'   66a5
enc 32 'stosb %al, %es:(%edi)'          aa
enc 32 'stosl %eax, %es:(%edi)'         ab
enc 32 'scasb %es:(%edi), %al'          ae
enc 32 'cmpsb %es:(%edi), (%esi)'       a6
enc 32 'lodsb (%esi), %al'              ac
enc 32 'insb (%dx), %es:(%edi)'         6c
enc 32 'insw (%dx), %es:(%edi)'         666d
enc 32 'insl (%dx), %es:(%edi)'         6d
enc 32 'ins (%dx), %es:(%edi)'          6d
enc 32 'outsb (%esi), (%dx)'            6e
enc 32 'outsw (%esi), (%dx)'            666f
enc 32 'outsl (%esi), (%dx)'            6f
enc 64 'movsb (%rsi), %es:(%rdi)'       a4
enc 64 'movsw (%rsi), %es:(%rdi)'       66a5
enc 64 'stosl %eax, %es:(%rdi)'         ab
enc 64 'insw (%dx), %es:(%rdi)'         666d
enc 64 'insl (%dx), %es:(%rdi)'         6d
enc 64 'outsw (%rsi), (%dx)'            666f
enc 64 'outsl (%rsi), (%dx)'            6f

# The source's segment is the prefix, wherever in the line it is.
enc 32 'movsb %fs:(%esi), %es:(%edi)'       64a4
enc 32 'lodsb %fs:(%esi), %al'              64ac
enc 32 'lodsl %gs:(%esi), %eax'             65ad
enc 32 'cmpsb %es:(%edi), %gs:(%esi)'       65a6
enc 32 'cmpsl %es:(%edi), %cs:(%esi)'       2ea7
enc 32 'outsb %cs:(%esi), (%dx)'            2e6e
enc 32 'rep movsb %fs:(%esi), %es:(%edi)'   64f3a4
enc 32 'rep movsw %fs:(%esi), %es:(%edi)'   6466f3a5
enc 64 'movsb %fs:(%rsi), %es:(%rdi)'       64a4
enc 64 'rep cmpsb %es:(%rdi), %gs:(%rsi)'   65f3a6

[ "$fail" -eq 0 ] && echo "ok: string instructions"
exit "$fail"
