#!/bin/sh
# The instructions with a VEX or an EVEX prefix (AS-T-311 to 336).
#
# They were encoded in a dozen places, each with a table of its own or
# none, and what one mnemonic became depended on which was asked first:
# `vblendvps` had its fourth register in the wrong half of its byte,
# `vpermilps` was in the wrong opcode map, `vprold` had its destination
# where its opcode extension goes, a write mask was dropped from an
# instruction with an AVX form, %zmm16 was "unknown register", `{sae}`
# was no operand, and AVX itself was refused unless -march asked for it.
# They are one table and one emitter (as_x86_vec.c, as_x86_vec_table.c).
#
#
# And then those of MMX and SSE, of BMI, and the few others the audit's
# corpus has (AS-T-316, 317, 335, 337 to 339), which went into the same
# table: they had taken any register for the one they wanted -- `paddb
# %ymm1,%ymm2` was `paddb %xmm1,%xmm2`, `pmaxsd (%rax),%mm2` an SSE4.1
# instruction on an MMX register -- and AES, SHA and `xrstors` were not
# assembled at all.
#
# vec_forms.txt holds, for each mnemonic of the audit's corpus, what
# GNU as 2.46 makes of it in each shape of its operands, in 64-bit and
# in 32-bit code: the bytes, or `refused`.  GNU as passes this test.
#
# The lines of a mode are assembled together, and the bytes of all of
# them compared; on a difference the first line that differs is found
# by the lengths GNU's have.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1
# vec_rules.txt is the same of the statements the tasks name: the
# registers past the fifteenth, the one-byte displacement and what it
# is multiplied by, masks, broadcasts and rounding and their misuse,
# the lengths of the conversions, gathers and scatters.
#
# sys_forms.txt is the same of the instructions of the system, of
# virtual machines and of the shadow stack, which are rows of the table
# too: the 32-bit emitter had most of them and the 64-bit one almost
# none -- `rdrand %eax`, `clflushopt (%rax)`, `xtest` and forty more
# were "unsupported mnemonic" in 64-bit code -- and neither had AMD's.
cat "$here/vec_forms.txt" "$here/vec_rules.txt" "$here/sys_forms.txt" > forms.txt
data=forms.txt

fail=0
cases=0

# What -march allows (not GNU's, which has no levels to refuse by): AVX
# at x86-64-v3, AVX-512 at x86-64-v4, and with none given, all of it.
level() {
    cases=$((cases + 1))
    printf '\t%s\n' "$3" > l.s
    if "$AS" --64 $1 -o l.o l.s 2> err; then got=taken; else got=$(sed 's/.*requires -march=\(x86-64-v[0-9]\).*/\1/' err | tail -1); fi
    [ "$got" = "$2" ] || { echo "FAIL $1 [$3]: $got, not $2"; fail=1; }
}
if ! "$AS" --version 2> /dev/null | grep -q '^GNU assembler'; then
    level "" taken 'vaddps %ymm1,%ymm2,%ymm3'
    level "" taken 'vfmadd231pd %ymm1,%ymm2,%ymm3'
    level "" taken 'vaddps %zmm1,%zmm2,%zmm3{%k1}'
    level -march=x86-64-v3 taken 'vsqrtps %ymm1,%ymm2'
    level -march=x86-64-v3 x86-64-v4 'vsqrtps %zmm1,%zmm2'
    level -march=x86-64-v3 x86-64-v4 'vaddps %xmm1,%xmm2,%xmm3{%k1}'
    level -march=x86-64-v3 x86-64-v4 'vrcp14ps %xmm1,%xmm2'
    level -march=x86-64-v2 x86-64-v3 'vsqrtps %ymm1,%ymm2'
    level -march=x86-64-v2 x86-64-v3 'vpbroadcastd %xmm0,%ymm1'
    level -march=x86-64-v2 x86-64-v4 'vsqrtps %zmm1,%zmm2'
    level -march=x86-64-v4 taken 'vsqrtps %zmm1,%zmm2'
fi

for mode in 64 32; do
    awk -F'\t' -v m="$mode" '$1 == m && $2 != "refused" { print $3 > "ok.s"; print $2 > "ok.hex" }
                             $1 == m && $2 == "refused" { print $3 > "bad.s" }' "$data"
    n=$(wc -l < ok.s)
    cases=$((cases + n))
    if ! "$AS" --$mode -o ok.o ok.s 2> err; then
        echo "FAIL --$mode: refused, of $n forms GNU as takes:"
        sed 's/^[^:]*:\([0-9]*\): *\(Error: \)\{0,1\}/\1 /' err | head -20 | while read -r ln why; do
            echo "  [$(sed -n "${ln}p" ok.s)] $why"
        done
        fail=1
    else
        objcopy -O binary --only-section=.text ok.o ok.bin
        od -An -v -tx1 ok.bin | tr -d ' \n' > got.hex
        tr -d '\n' < ok.hex > want.hex
        if ! cmp -s got.hex want.hex; then
            # The first line whose bytes are not GNU's, by GNU's lengths.
            awk 'NR == FNR { got = $0; next }
                 { n = length($0); if (substr(got, at + 1, n) != $0) { print FNR "\t" substr(got, at + 1, n) "\t" $0; exit } at += n }' \
                got.hex ok.hex > first
            IFS='	' read -r ln got want < first
            echo "FAIL --$mode [$(sed -n "${ln}p" ok.s)]: $got, and GNU as: $want"
            fail=1
        fi
    fi
    # What GNU refuses, a hundred lines at a time: each is refused by
    # its own line.
    n=$(wc -l < bad.s)
    cases=$((cases + n))
    split -l 90 bad.s badpart.
    for part in badpart.*; do
        "$AS" --$mode -o bad.o "$part" 2> err
        sed -n 's/^[^:]*:\([0-9]*\):.*/\1/p' err | sort -un > refused
        awk 'NR == FNR { r[$1] = 1; next } !(FNR in r) { print }' refused "$part" > taken
        if [ -s taken ]; then
            echo "FAIL --$mode: assembled, and GNU as refuses:"
            head -20 taken | sed 's/^/  /'
            fail=1
        fi
    done
    rm -f ok.s ok.hex bad.s badpart.* ok.o bad.o
done

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
