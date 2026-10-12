#!/bin/sh
# --help and --target-help (AS-T-356): what they list is what there is.
#
# The usage line was one line of some of the options, written to the
# standard error when the assembler was run wrongly, and there was no
# --help; --target-help said of x86 "basic x87/MMX/SSE names" long after
# AVX-512 was assembled, and of ARM "baseline syntax" when nothing of
# ARM was assembled at all.
#
# Each option --help lists is tried and must be taken; and each option
# the assembler's argument loop compares an argument with must be in
# what --help prints.  The second is read out of the source, there being
# no other list of what is taken.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
case $AS in /*) ;; *) AS=$(pwd)/$AS ;; esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

if "$AS" --version 2> /dev/null | grep -q '^GNU assembler'; then
    echo "ok: --help (not tried: the list is this assembler's own)"
    exit 0
fi

fail=0
cases=0
bad() {
    echo "FAIL $*"
    fail=1
}

printf '\t.text\n\tnop\n' > x.s
printf '\tnop\n' > arm.s

"$AS" --help > help.out 2> help.err
[ $? -eq 0 ] || bad "--help: does not exit 0"
[ -s help.err ] && bad "--help: writes to the standard error"
grep -q '^usage: ' help.out || bad "--help: no usage line"
"$AS" --help -o h.o x.s > /dev/null 2>&1
[ -e h.o ] && bad "--help with a source: assembles it"

# Run wrongly, it says where the list is, on the standard error.
"$AS" -o > u.out 2> u.err
[ $? -eq 2 ] || bad "no source: does not exit 2"
grep -q -- '--help' u.err || bad "no source: does not say that --help lists the options"

# try OPTION...: taken, with a source, as far as an option is concerned.
try() {
    cases=$((cases + 1))
    rm -f t.o
    "$AS" "$@" -o t.o x.s > t.out 2> t.err
    if grep -q 'unrecognized option\|requires an argument\|^usage:' t.err; then
        bad "listed and not taken: $* ($(head -1 t.err))"
    fi
}

# Each line of the list: the option's spellings, then what it is.  An
# argument is written for the ones that take one.
sed -n 's/^  \(-.*\)$/\1/p' help.out > lines
[ "$(wc -l < lines)" -ge 30 ] || bad "--help lists $(wc -l < lines) options"
while IFS= read -r line; do
    spellings=$(printf '%s\n' "$line" | sed 's/   *.*$//')
    printf '%s\n' "$spellings" | tr ',' '\n' | sed 's/^ *//' | while IFS= read -r one; do
        [ -n "$one" ] || continue
        opt=${one%% *}
        arg=${one#"$opt"}
        arg=${arg# }
        case $opt in
        -o) continue ;;                           # (every try has one)
        --help | --target-help | --version | -v) "$AS" "$opt" > o.out 2> o.err || echo "FAIL $opt: exit $?" ;;
        --target=NAME) try --target=i386 ;;
        -march=*) try -march=x86-64-v3; try -march x86-64 ;;
        -mtune=*) try -mtune=generic; try -mtune generic ;;
        -msyntax=*) try -msyntax=att; try -msyntax intel ;;
        -mthumb | -marm) "$AS" --target=arm "$opt" -o t.o arm.s 2> t.err || echo "FAIL $opt: $(head -1 t.err)" ;;
        -mcpu=* | -mfpu=* | -mfloat-abi=*) "$AS" --target=arm "${opt%%=*}=x" -o t.o arm.s 2> t.err || echo "FAIL $opt: $(head -1 t.err)" ;;
        -O) try -O elf; try -Obinary ;;
        --gdwarf-N) try --gdwarf-5 ;;
        -I) try -I . ; try -I. ;;
        -D) try -D X=1; try -DX ;;
        --defsym) try --defsym X=1 ;;
        '-al[=FILE]') try -al > /dev/null; try -al=l.lst ;;
        -Wa*) try -Wa,--32; try -Wa --32 ;;
        --max-*) try "$opt" 1000000; try "$opt=1000000" ;;
        *)
            [ -z "$arg" ] || echo "FAIL $opt: its argument $arg is not known to this test"
            try "$opt"
            ;;
        esac
    done
done < lines > tried.out 2>&1
grep '^FAIL' tried.out && fail=1
cases=$((cases + $(wc -l < lines)))

# Each option the argument loop knows is in the list.  They are the
# strings an argument is compared with; one ending in `=` or compared
# by its first letters is listed by its stem.
src="$top/usr.bin/as/as.c"
if [ -r "$src" ]; then
    grep -o 'str[n]*cmp(arg, "-[^"]*"' "$src" | sed 's/.*"\(-[^"]*\)"/\1/' | sort -u > known
    [ "$(wc -l < known)" -ge 40 ] || bad "the source names $(wc -l < known) options, which is too few to be all of them"
    while IFS= read -r opt; do
        cases=$((cases + 1))
        stem=${opt%=}
        grep -q -- "[ ,]$stem\([= ,[]\|\$\|-N\)" help.out ||
            grep -q -- "^  $stem\([= ,[]\|\$\)" help.out ||
            bad "taken and not listed: $opt"
    done < known
    grep -q -- '-Wa' help.out || bad "taken and not listed: -Wa"
fi

# --target-help: each machine --target takes, and no other.
"$AS" --target-help > th.out 2> th.err
[ $? -eq 0 ] || bad "--target-help: does not exit 0"
for name in i386 x86_64 arm aarch64; do
    cases=$((cases + 1))
    grep -q -- "--target=$name" th.out || bad "--target-help does not name --target=$name"
    "$AS" "--target=$name" -o t.o arm.s 2> t.err || bad "--target=$name is listed and not taken: $(head -1 t.err)"
done
for name in $(grep -o -- '--target=[a-z0-9_]*' th.out | sort -u | sed 's/--target=//'); do
    "$AS" "--target=$name" -o t.o arm.s 2> t.err || bad "--target-help names --target=$name, which is not taken"
done
for word in mips sparc riscv ppc 'baseline' 'basic x87'; do
    grep -qi -- "$word" th.out && bad "--target-help speaks of $word"
done
# What it says of the flat image is so.
"$AS" --target=arm -O binary -o t.bin arm.s 2> t.err && bad "a flat image is written for ARM, and --target-help says x86 only"

# as(1) (AS-T-358): every option of the list, and every directive the
# assembler knows by name, is in the manual; and the manual is sound.
man="$top/usr.man/man1/as.1"
if [ -r "$man" ] && command -v mandoc > /dev/null 2>&1; then
    mandoc -T ascii "$man" 2> /dev/null | col -b > as.txt
    mandoc -T lint -W warning "$man" > lint.out 2>&1 || bad "as(1) does not pass mandoc -T lint: $(head -1 lint.out)"
    sed 's/   *.*$//' lines | tr ',' '\n' | sed 's/^ *//; s/[= [].*$//' | grep '^-' | sort -u > opts
    while IFS= read -r opt; do
        cases=$((cases + 1))
        grep -q -- "$opt" as.txt || bad "as(1) does not describe $opt"
    done < opts
    if [ -r "$src" ]; then
        sed -n '/^static int directive_is_known/,/^}/p' "$src" | grep -o '"\.[a-z0-9_.]*"' | tr -d '"' | sort -u > dirs
        [ "$(wc -l < dirs)" -ge 60 ] || bad "the source names $(wc -l < dirs) directives, which is too few to be all of them"
        while IFS= read -r d; do
            cases=$((cases + 1))
            grep -q -- "$(printf '%s' "$d" | sed 's/\./\\./g')\([^a-z0-9_]\|\$\)" as.txt || bad "as(1) does not describe $d"
        done < dirs
    fi
    for limit in max-input-bytes max-line-bytes max-token-length max-macro-depth max-include-depth max-expansion-bytes; do
        grep -q -- "$limit" as.txt || bad "as(1) does not describe the limit --$limit"
    done
fi

if [ "$fail" = 0 ]; then
    echo "ok: --help and --target-help ($cases cases)"
    exit 0
fi
exit 1
