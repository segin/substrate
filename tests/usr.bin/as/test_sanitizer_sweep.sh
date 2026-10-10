#!/bin/sh
# Every mnemonic the x86 encoder names, with operands it does not take,
# through a build with AddressSanitizer and UndefinedBehaviorSanitizer
# (AS-T-022, and the start of AS-T-017).
#
# `inc` alone on a line dereferenced a null pointer: the encoder read the
# register of its first operand before counting its operands.  An absent
# operand is now an empty one and not a null pointer; this holds every
# branch to that, by giving each mnemonic no operand, an immediate, a
# memory operand and four registers, in both modes.  Whether a line
# assembles is not looked at here -- only that the assembler ends by
# assembling it or refusing it, with nothing from either sanitizer.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if ! ${CC:-cc} -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined \
        -o "$work/as" \
        -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
        -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/as/*.c "$top"/usr.lib/elfobj/src/*.c \
        2> "$work/build.err"; then
    # A compiler without the sanitizers' runtime is not a failure of the
    # assembler.
    echo "skip: no sanitizer build ($(head -1 "$work/build.err"))"
    exit 0
fi

cd "$work" || exit 1
grep -o 'streq_ci(insn->mnemonic, "[a-z0-9]*")' "$top/usr.bin/as/as_x86_encode.c" |
    sed 's/.*"\(.*\)".*/\1/' | sort -u > mnemonics
[ "$(wc -l < mnemonics)" -gt 400 ] || { echo "FAIL: the mnemonic list was not found in the encoder"; exit 1; }

# One source for each shape and mode, a line a mnemonic: the assembler
# stops at the first line it refuses, so each line is a run of its own.
fail=0
runs=0
for mode in --32 --64; do
    while read -r mn; do
        for ops in '' '$1' '(%eax)' '%eax, %eax, %eax, %eax'; do
            printf '\t%s %s\n' "$mn" "$ops" > t.s
            ASAN_OPTIONS=detect_leaks=0 ./as "$mode" -o t.o t.s > out 2>&1
            rc=$?
            runs=$((runs + 1))
            if [ "$rc" -gt 1 ] || grep -q 'AddressSanitizer\|runtime error' out; then
                echo "FAIL $mode '$mn $ops': exit $rc: $(grep -m1 'runtime error\|ERROR: AddressSanitizer' out)"
                fail=1
            fi
        done
    done < mnemonics
done

# A pseudo-prefix that ends its line (AS-T-023).  The lexer compared the
# text with "{vex}" for six characters -- the terminator among them, so
# it matched only here -- and then stepped six on, past the end of the
# line.  With and without a final newline.
for text in '{vex}' '{evex}' '{disp8}' '{disp32}' 'nop {vex}' '{' '{vex' '}'; do
    for nl in '\n' ''; do
        printf "%s$nl" "$text" > t.s
        ASAN_OPTIONS=detect_leaks=0 ./as --64 -o t.o t.s > out 2>&1
        rc=$?
        runs=$((runs + 1))
        if [ "$rc" -gt 1 ] || grep -q 'AddressSanitizer\|runtime error' out; then
            echo "FAIL '$text': exit $rc: $(grep -m1 'runtime error\|ERROR: AddressSanitizer' out)"
            fail=1
        fi
    done
done

[ "$fail" -eq 0 ] && echo "ok: sanitizer sweep ($runs lines)"
exit "$fail"
