#!/bin/sh
# Assembling for ARM and for AArch64 (AS-T-370, 371, 374 to 381, 383 to
# 387).
#
# Run as arm-as or aarch64-as, or with --target naming one, the
# assembler reads that machine's source and writes that machine's
# object.  It had the names and wrote x86 under them -- `ret` as the
# byte C3 in an x86-64 object -- and was then made to refuse them; this
# is the other answer.
#
# The words are llvm-mc 20's (test_arm_forms.sh has the corpus); here
# are the statements the tasks name, what is refused and why, the
# literal pool, the directives, and the objects.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
case $AS in /*) ;; *) AS=$(pwd)/$AS ;; esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

if "$AS" --version 2> /dev/null | grep '^GNU assembler' | grep -qv '(Substrate)'; then
    echo "ok: ARM targets (not tried: GNU as for x86 has no ARM)"
    exit 0
fi
ln -s "$AS" arm-as
ln -s "$AS" aarch64-as

fail=0
cases=0
bad() {
    echo "FAIL $*"
    fail=1
}

# The bytes of a section, in the order they are in.
section() {
    readelf -S -W "$1" | awk -v name="$2" '
        { for (i = 1; i <= NF; i++) if ($i == name) { print strtonum("0x" $(i + 3)), strtonum("0x" $(i + 4)); exit } }' > sec
    read -r off size < sec || return 1
    [ "${size:-0}" != 0 ] || return 0
    od -An -v -tx1 -j "$off" -N "$size" "$1" | tr -d ' \n'
}

# words AS SOURCE WANT [options]: the source's .text is the bytes WANT.
words() {
    cases=$((cases + 1))
    as=$1
    printf '%b' "$2" > w.s
    rm -f w.o
    # shellcheck disable=SC2086
    if ! ./"$as" ${4:-} -o w.o w.s 2> err; then
        bad "$as [$2]: refused: $(head -1 err)"
        return
    fi
    got=$(section w.o .text)
    [ "$got" = "$3" ] || bad "$as [$2]: $got, not $3"
}

# refused AS SOURCE PATTERN: no object, exit 1, and the reason.
refused() {
    cases=$((cases + 1))
    printf '%b' "$2" > r.s
    rm -f r.o
    ./"$1" -o r.o r.s 2> err
    rc=$?
    [ "$rc" = 1 ] || bad "$1 [$2]: exit $rc, not 1"
    [ ! -e r.o ] || bad "$1 [$2]: an object was written"
    grep -q "$3" err || bad "$1 [$2]: the message is not about '$3': $(head -1 err)"
}

machine() {
    readelf -h "$1" | sed -n 's/^ *Machine: *//p'
}

# 371: the machine of the object, by the name run as and by --target.
words arm-as '\tadd r0, r0, r1\n' 010080e0
[ "$(machine w.o)" = ARM ] || bad "arm-as: the object's machine is $(machine w.o)"
readelf -h w.o | grep -q 'ELF32' || bad "arm-as: the object is not ELF32"
readelf -h w.o | grep -q 'Flags:.*0x5000000' || bad "arm-as: the object does not say it is of the EABI"
words aarch64-as '\tret\n' c0035fd6
[ "$(machine w.o)" = AArch64 ] || bad "aarch64-as: the object's machine is $(machine w.o)"
readelf -h w.o | grep -q 'ELF64' || bad "aarch64-as: the object is not ELF64"
printf '\tadd r0, r0, r1\n' > t.s
"$AS" --target=arm -o t.o t.s && [ "$(machine t.o)" = ARM ] || bad "--target=arm"
printf '\tret\n' > t.s
"$AS" --target aarch64 -o t.o t.s && [ "$(machine t.o)" = AArch64 ] || bad "--target aarch64"
"$AS" --target=x86_64 -o t.o t.s && [ "$(section t.o .text)" = c3 ] || bad "--target=x86_64"
if "$AS" --target=sparc -o t.o t.s 2> err; then bad "--target=sparc assembles"; fi
# Which processor, and Thumb from the first line.
words arm-as '\tadds r0, r1, r2\n' 8818 "-mthumb -mcpu=cortex-a8 -march=armv7-a -mfpu=neon"
if "$AS" -mthumb -o t.o t.s 2> err; then bad "-mthumb with no ARM target assembles"; fi
# The code stands where a word may.
[ "$(readelf -S -W w.o | awk '$2 == ".text" || $3 == ".text" { print $(NF) }')" = 4 ] ||
    bad "arm-as: .text is not aligned to four bytes"

# 374: what cannot be encoded is refused, with the reason.
refused arm-as '\tadd r0, r1, #0x101\n' 'eight bits, turned by an even count'
refused arm-as '\tmov r0, #0x12345\n' 'eight bits, turned by an even count'
refused arm-as '\tmovw r3, #0x10000\n' 'a number of 16 bits'
refused arm-as '\tb far\n\t.org 0x4000000\nfar:\tnop\n' 'bytes away, which it cannot reach'
refused arm-as '\tb odd\n\tnop\n\t.byte 1\nodd:\tnop\n' 'not at a multiple of four bytes'
refused aarch64-as '\tb far\n\t.org 0x10000000\nfar:\tnop\n' 'bytes away, which it cannot reach'
refused aarch64-as '\tcbz x0, odd\n\t.byte 1\nodd:\tnop\n' 'not at a multiple of four bytes'
refused aarch64-as '\tadd x0, x1, #0x1001\n' 'twelve bits'
refused aarch64-as '\tand x0, x1, #0x1234\n' 'a run of ones'
refused arm-as '\tfrob r0\n' 'not an instruction of ARM'
refused aarch64-as '\tfrob x0\n' 'not an instruction of AArch64'

# 375: the shifts by 32 and by nothing.
words arm-as '\tmov r0, r1, lsr #32\n\tmov r0, r1, asr #32\n\tmov r0, r1, rrx\n' 2100a0e14100a0e16100a0e1
words arm-as '\tmov r0, r1, lsr #0\n\tmov r0, r1, asr #0\n\tmov r0, r1, ror #0\n' 0100a0e10100a0e10100a0e1
refused arm-as '\tmov r0, r1, lsl #32\n' 'a shift of 32'
refused arm-as '\tmov r0, r1, ror #32\n' 'a shift of 32'

# 376: a register that is none is refused in each field.
refused arm-as '\tadd r16, r1, r2\n' 'register'
refused arm-as '\tadd r0, r16, r2\n' 'register'
refused arm-as '\tadd r0, r1, r16\n' 'register'
refused arm-as '\tadd r0, r1, r2, lsl r16\n' 'shift'
refused arm-as '\tmul r0, r1, r16\n' 'register'
refused arm-as '\tldr r16, [r1]\n' 'register'
refused arm-as '\tldr r0, [r16]\n' 'register'
refused arm-as '\tldr r0, [r1, r16]\n' 'register'
refused arm-as '\tpush {r4, r16}\n' 'list of registers'
refused arm-as '\tvadd.f64 d32, d0, d1\n' 'VFP or NEON'
refused arm-as '\tvadd.f32 s0, s32, s1\n' 'three registers of one kind'
refused arm-as '\tvadd.i8 q16, q0, q1\n' 'VFP or NEON'

# 377: a constant one instruction cannot move is in the literal pool,
# at .ltorg and at the end of the section, and is loaded from there;
# the same constant is there once; one that can be moved is moved.
words arm-as '\t.syntax unified\n_start:\tldr r0, =0x12345678\n\tldr r1, =0x1000\n\tldr r2, =0x12345678\n\tbx lr\n\t.ltorg\n\tldr r3, =sym\n\tbx lr\n' \
    08009fe5011aa0e300209fe51eff2fe17856341200309fe51eff2fe100000000
readelf -r w.o | grep -q '0000001c .*R_ARM_ABS32 .*sym' || bad "the pool's word for a symbol has no relocation"
words aarch64-as '_start:\tldr x0, =0x123456789abcdef0\n\tldr w1, =0x12345678\n\tldr x2, =0x1000\n\tldr x3, =sym\n\tret\n' \
    c0000058e1000018020082d2e3000058c0035fd600000000f0debc9a7856341278563412000000000000000000000000
readelf -r w.o | grep -q '000000000028 .*R_AARCH64_ABS64 .*sym' || bad "the AArch64 pool's word for a symbol has no relocation"
words arm-as '\tldr r0, =1\n\tbx lr\n\t.pool\n' 0100a0e31eff2fe1

# 378: an offset taken away, and the base written back before and after.
words arm-as '\tldr r0, [r1, #-4]\n\tldr r0, [r1], #4\n\tstr r0, [r1, #-4]!\n' 040011e5040091e4040021e5

# 379: the stack names, for a load and for a store; push and pop.
words arm-as '\tldmfd sp!, {r4, r5}\n\tldmea r0, {r1, r2}\n\tldmed r0, {r1, r2}\n\tldmfa r0, {r1, r2}\n' \
    3000bde8060010e9060090e9060010e8
words arm-as '\tstmfd sp!, {r4, lr}\n\tstmea r0, {r1, r2}\n\tstmed r0, {r1, r2}\n\tstmfa r0, {r1, r2}\n' \
    10402de9060080e8060000e8060080e9
words arm-as '\tpush {r0-r3, r12, lr}\n\tpop {r4}\n\tpush {r4}\n\tpop {r0, r1, pc}\n' 0f502de904409de404402de50380bde8

# 380: a mnemonic's name, its `s` and its condition, in either order.
words arm-as 'back:\tteq r0, r1\n\tsvc #1\n\tmrs r0, apsr\n\tmovseq r0, r1\n\tmoveqs r0, r1\n' \
    010030e1010000ef00000fe10100b0010100b001
words arm-as 'back:\tbls back\n\tbleq back\n\tblt back\n\tblo back\n' feffff9afdffff0bfcffffbafbffff3a
if grep -q 'parse_arm_condition' "$top/usr.bin/as/as_parser.c"; then
    bad "the parser has its own idea of an ARM mnemonic's condition"
fi

# 381: a hint with a condition.
words arm-as '\tnopeq\n\twfine\n' 00f0200303f02013

# 382: Thumb sets the flags by the mnemonic and by the IT block, and the
# two halves of a long instruction are the first first.
words arm-as '\t.syntax unified\n\t.thumb\n\tadds r0, r1, r2\n\tadd r0, r1, r2\n\tit eq\n\taddeq r0, r1, r2\n\torn r0, r1, r2\n' \
    881801eb020008bf881861ea0200

# 383, 384: AArch64's return and its branches.
words aarch64-as 'back:\tret\n\tret x1\n\tcbz x0, back\n\tb.ne back\n\tb.hs back\n\tbne back\n' \
    c0035fd620005fd6c0ffffb4a1ffff5482ffff5461ffff54

# 385: AArch64's registers, and what is none.
words aarch64-as '\tmov x0, x30\n\tadd x29, sp, #0\n\tmov w0, wzr\n\tmov x1, xzr\n\tmov x0, lr\n\tmov x0, fp\n\tmov x0, ip0\n\tmov x1, ip1\n\tmov v0.16b, v1.16b\n\tfmov d0, d1\n\tadd wsp, wsp, #4\n' \
    e0031eaafd030091e0031f2ae1031faae0031eaae0031daae00310aae10311aa201ca14e2040601eff130011
refused aarch64-as '\tmov x0, x05\n' 'not a register'
refused aarch64-as '\tmov x0, x007\n' 'not a register'
refused aarch64-as '\tmov x0, x31\n' 'not a register'
refused aarch64-as '\tmov w0, w31\n' 'not a register'

# 386: no relocation is named that the ABI does not define.
if grep -q 'R_AARCH64_MOVW_UABS_G3_NC' "$top"/usr.bin/as/*.c "$top"/usr.bin/as/*.h; then
    bad "a relocation is named that AArch64's ABI does not have"
fi
words aarch64-as '\tadrp x0, sym\n\tadd x0, x0, :lo12:sym\n\tldr x1, [x0, :lo12:sym]\n\tbl sym\n\tb sym\n\tb.eq sym\n\ttbz x0, #1, sym\n\tmovz x0, #:abs_g3:sym\n' \
    0000009000000091010040f9000000940000001400000054000008360000e0d2
for r in '000000000000 .*R_AARCH64_ADR_PREL_PG_HI21' '000000000004 .*R_AARCH64_ADD_ABS_LO12_NC' \
    '000000000008 .*R_AARCH64_LDST64_ABS_LO12_NC' '00000000000c .*R_AARCH64_CALL26' '000000000010 .*R_AARCH64_JUMP26' \
    '000000000014 .*R_AARCH64_CONDBR19' '000000000018 .*R_AARCH64_TSTBR14' '00000000001c .*R_AARCH64_MOVW_UABS_G3'; do
    readelf -r -W w.o | grep -q "$r" || bad "aarch64-as: no relocation $r"
done
words arm-as '\tbl sym\n\tb sym\n\tbeq sym\n\tmovw r0, #:lower16:sym\n\tmovt r0, #:upper16:sym\n\t.data\n\t.word sym\n\t.long sym - .\n' \
    feffffebfeffffeafeffff0a000000e3000040e3
for r in '00000000 .*R_ARM_CALL' '00000004 .*R_ARM_JUMP24' '00000008 .*R_ARM_JUMP24' '0000000c .*R_ARM_MOVW_ABS_NC' \
    '00000010 .*R_ARM_MOVT_ABS' '00000000 .*R_ARM_ABS32' '00000004 .*R_ARM_REL32'; do
    readelf -r -W w.o | grep -q "$r" || bad "arm-as: no relocation $r"
done

# 387: the directives.
words aarch64-as '\t.inst 0xd65f03c0\n' c0035fd6
words arm-as '\t.word 0xe12fff1e\n' 1eff2fe1
words arm-as '\t.inst 0xe12fff1e\n' 1eff2fe1
words aarch64-as '\t.word 0x11223344\n' 44332211
words arm-as '\t.arch armv7-a\n\t.cpu cortex-a8\n\t.fpu neon\n\t.syntax unified\n\tnop\n' 00f020e3
words aarch64-as '\t.arch armv8-a\n\t.cpu cortex-a53\n\tnop\n' 1f2003d5
# .thumb and .arm: the mode, and where an instruction of it may stand.
words arm-as '\t.syntax unified\n\t.thumb\n\tbx lr\n\t.arm\n\tbx lr\n\t.code 16\n\tbx lr\n\t.code 32\n\tbx lr\n' \
    704700001eff2fe1704700001eff2fe1
words arm-as '\t.thumb\n\t.inst 0x4770\n\t.inst.n 0xbf00\n\t.inst.w 0xf3af8000\n\t.inst 0xf3af8000\n' 704700bfaff30080aff30080
# .thumb_func, and a function of Thumb code: the low bit of the symbol.
printf '\t.syntax unified\n\t.thumb\n\t.thumb_func\nf:\tbx lr\n\t.type g, %%function\ng:\tbx lr\nh:\tbx lr\n\t.arm\n\t.type a, %%function\na:\tbx lr\n' > f.s
./arm-as -o f.o f.s 2> err || bad ".thumb_func: $(head -1 err)"
readelf -s -W f.o > syms
grep -q '00000001 .* FUNC .* f$' syms || bad ".thumb_func: f is not a Thumb function"
grep -q '00000003 .* FUNC .* g$' syms || bad "a function of Thumb code has not its low bit"
grep -q '00000004 .* NOTYPE .* h$' syms || bad "a label of Thumb code that is no function has the low bit"
grep -q '00000008 .* FUNC .* a$' syms || bad "a function of ARM code has the low bit"
# .ltorg and .pool are the pool (above).  .eabi_attribute is its section.
printf '\t.eabi_attribute 6, 10\n\t.eabi_attribute 8, 1\n\t.eabi_attribute 5, "cortex-a8"\n\tnop\n' > a.s
./arm-as -o a.o a.s 2> err || bad ".eabi_attribute: $(head -1 err)"
[ "$(section a.o .ARM.attributes)" = 411e0000006165616269000114000000060a080105636f727465782d613800 ] ||
    bad ".eabi_attribute: .ARM.attributes is $(section a.o .ARM.attributes)"
readelf -S -W a.o | grep -q 'ARM.attributes *ARM_ATTRIBUTES' || bad ".ARM.attributes has not its section type"
# The unwinding tables are not made, and their directives say so.
refused arm-as '\t.fnstart\n' 'unwinding'
refused arm-as '\t.fnend\n' 'unwinding'
refused arm-as '\t.syntax apcs\n' 'unified or divided'
refused arm-as '\t.code 64\n' '16 for Thumb or 32 for ARM'
refused arm-as '\t.arch\n' 'which one'
refused aarch64-as '\t.thumb\n' 'a directive of ARM'
refused aarch64-as '\t.syntax unified\n' 'a directive of ARM'
refused arm-as '\t.inst.n 0xe12fff1e\n' 'Thumb'
# Comments: @ in ARM and // in both; # is before a number.
words arm-as '\tmov r0, #1 @ one\n\tmov r1, #2 // two\n# a line\n' 0100a0e30210a0e3
words aarch64-as '\tmov x0, #1 // one\n# a line\n' 200080d2
# Numbered labels.
words arm-as '1:\tb 1b\n\tb 1f\n1:\tnop\n' feffffeaffffffea00f020e3

# The code runs.
if command -v qemu-arm > /dev/null 2>&1 && command -v ld.lld > /dev/null 2>&1; then
    cases=$((cases + 1))
    cat > run.s <<'EOF'
        .syntax unified
        .text
        .globl _start
_start: mov r0, #1
        ldr r1, =msg
        ldr r2, =len
        ldr r2, [r2]
        mov r7, #4              @ write
        svc #0
        ldr r4, =0x12345678
        ldr r6, =0x12345600
        add r6, r6, #0x78
        subs r0, r4, r6         @ 0 if the pool held what was written
        mov r7, #1              @ exit
        svc #0
        .ltorg
        .data
msg:    .ascii "pool ok\n"
len:    .word . - msg
EOF
    ./arm-as -o run.o run.s && ld.lld -o run run.o && [ "$(qemu-arm ./run; echo "$?")" = "pool ok
0" ] || bad "the ARM program with a literal pool does not run"
fi
if command -v qemu-aarch64 > /dev/null 2>&1 && command -v ld.lld > /dev/null 2>&1; then
    cases=$((cases + 1))
    cat > run64.s <<'EOF'
        .text
        .globl _start
_start: mov x0, #1
        ldr x1, =msg
        mov x2, #10
        mov x8, #64             // write
        svc #0
        ldr x4, =0x123456789abcdef0
        ldr x5, =0xdef0
        movk x5, #0x9abc, lsl #16
        movk x5, #0x5678, lsl #32
        movk x5, #0x1234, lsl #48
        sub x0, x4, x5
        mov x8, #93             // exit
        svc #0
        .data
msg:    .ascii "pool64 ok\n"
EOF
    ./aarch64-as -o run64.o run64.s && ld.lld -o run64 run64.o && [ "$(qemu-aarch64 ./run64; echo "$?")" = "pool64 ok
0" ] || bad "the AArch64 program with a literal pool does not run"
fi

if [ "$fail" = 0 ]; then
    echo "ok: ARM targets ($cases cases)"
    exit 0
fi
exit 1
