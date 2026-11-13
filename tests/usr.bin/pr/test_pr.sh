#!/bin/sh
# pr(1) against the output POSIX.1-2024 specifies.  Where POSIX leaves a
# detail open (how columns are padded, what a form feed in the input
# does) the expected output is GNU pr's.  Header dates come from files
# whose modification time is set, in UTC and the POSIX locale.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
PR=${1:-"$ROOT/usr.bin/pr/pr"}
case $PR in
/*) ;;
*) PR=$(pwd)/$PR ;;
esac

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM
cd "$WORK" || exit 1

TZ=UTC
LC_ALL=C
export TZ LC_ALL

fail=0
cases=0

# expect LABEL WANT CMD...: CMD's standard output is WANT, its escapes
# (\n, \t, \f, \0NNN) interpreted.
expect() {
    label=$1
    want=$2
    shift 2
    cases=$((cases + 1))
    "$@" > got 2> err
    printf '%b' "$want" > want
    if ! cmp -s got want; then
        echo "FAIL $label"
        echo "  got : $(od -An -c got | head -6)"
        echo "  want: $(od -An -c want | head -6)"
        fail=1
    fi
}

# status LABEL WANT CMD...: CMD exits WANT (0, or "nonzero").
status() {
    label=$1
    want=$2
    shift 2
    cases=$((cases + 1))
    "$@" > /dev/null 2> err
    rc=$?
    if [ "$want" = 0 ] && [ $rc != 0 ] || [ "$want" != 0 ] && [ $rc = 0 ]; then
        echo "FAIL $label: exit $rc"
        fail=1
    fi
}

# stderr_has LABEL TEXT: the last command's standard error holds TEXT.
stderr_has() {
    cases=$((cases + 1))
    if ! grep -F -- "$2" err > /dev/null; then
        echo "FAIL $1: standard error [$(cat err)] lacks [$2]"
        fail=1
    fi
}

stamp() {
    touch -d '2020-01-02 03:04' "$@"
}

printf 'a\n' > one
printf 'x\ny\n' > xy
seq 1 4 > n4
seq 1 12 > n12
printf 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n' > long
stamp one xy n4 n12
HDR1='\n\nJan  2 03:04 2020 one Page 1\n\n\n'
nl10() { printf '\\n%.0s' $(seq 1 "$1"); }

# Pages: a 5-line header, the text, padding to the page length, a
# 5-line trailer of blank lines.
expect "66-line page" "${HDR1}a\n$(nl10 60)" "$PR" one
expect "-l 15, two pages" \
    "\n\nJan  2 03:04 2020 n12 Page 1\n\n\n1\n2\n3\n4\n5\n$(nl10 5)\n\nJan  2 03:04 2020 n12 Page 2\n\n\n6\n7\n8\n9\n10\n$(nl10 5)\n\nJan  2 03:04 2020 n12 Page 3\n\n\n11\n12\n$(nl10 8)" \
    "$PR" -l 15 n12
expect "-h" "\n\nJan  2 03:04 2020 Report Page 1\n\n\nx\ny\n$(nl10 9)" "$PR" -l 16 -h Report xy
# -l not over 10: no header or trailer, as with -t
expect "-l 10 is -t" "1\n2\n3\n4\n" "$PR" -l 10 n4
expect "-l 5 is -t" "1\n2\n3\n4\n" "$PR" -l 5 n4
expect "-t" "1\n2\n3\n4\n" "$PR" -t n4
# standard input: the current time and no name
cases=$((cases + 1))
printf 'x\n' | "$PR" -l 12 > got
if ! sed -n 3p got | grep -E '^[A-Z][a-z]{2} [ 0-9][0-9] [0-9]{2}:[0-9]{2} [0-9]{4}  Page 1$' > /dev/null; then
    echo "FAIL stdin header: [$(sed -n 3p got)]"
    fail=1
fi
expect "stdin -" "x\ny\n" sh -c "\"$PR\" -t - < xy"
# an empty file writes nothing
: > empty
expect "empty file" "" "$PR" empty
expect "-m of empty files" "" "$PR" -m empty empty

# Form feeds in the input start a page; with -t they are passed on.
printf 'a\fb\n' > ff
printf 'a\n\f\nb\n' > ff2
stamp ff ff2
expect "form feed" "\n\nJan  2 03:04 2020 ff Page 1\n\n\na\n$(nl10 7)\n\nJan  2 03:04 2020 ff Page 2\n\n\nb\n$(nl10 7)" "$PR" -l 13 ff
expect "form feed line" "\n\nJan  2 03:04 2020 ff2 Page 1\n\n\na\n$(nl10 7)\n\nJan  2 03:04 2020 ff2 Page 2\n\n\nb\n$(nl10 7)" "$PR" -l 13 ff2
expect "-t form feed" "a\n\fb\n" "$PR" -t ff
expect "-t form feed line" "a\n\fb\n" "$PR" -t ff2

# Bytes are passed through.
printf 'a\r\n' > cr
printf 'a\000b\n' > nul
expect "carriage return" "a\r\n" "$PR" -t cr
expect "NUL" "a\000b\n" "$PR" -t nul

# Columns: equal widths ((width - separators) / columns), separated by
# at least one blank, spaces to tabs (-i) and tabs to spaces (-e).
expect "-2 down" "1\t\t\t\t    3\n2\t\t\t\t    4\n" "$PR" -t -2 n4
expect "-2 -a across" "1\t\t\t\t    2\n3\t\t\t\t    4\n" "$PR" -t -2 -a n4
expect "-3 balanced" "1\t\t\t5\t\t\t9\n2\t\t\t6\t\t\t10\n3\t\t\t7\t\t\t11\n4\t\t\t8\t\t\t12\n" "$PR" -t -3 n12
expect "columns fill the page" \
    "\n\nJan  2 03:04 2020 n12 Page 1\n\n\n1\t\t\t\t    3\n2\t\t\t\t    4\n$(nl10 5)\n\nJan  2 03:04 2020 n12 Page 2\n\n\n5\t\t\t\t    7\n6\t\t\t\t    8\n$(nl10 5)\n\nJan  2 03:04 2020 n12 Page 3\n\n\n9\t\t\t\t    11\n10\t\t\t\t    12\n$(nl10 5)" \
    "$PR" -2 -l 12 n12
expect "every column cut" "aaaaaaaaa bbbbbbbbb\n" "$PR" -t -2 -w 20 long
expect "-3 -w 20" "aaaaaa bbbbbb\n" "$PR" -t -3 -w 20 long
printf 'a\tb\n1\n' > tab
# expanded, the column cut to its width, then spaces to tabs again
printf 'a\tb\tc\td\te\tf\tg\n1\n' > tabs
expect "input tabs expanded" "a\tb\t\t\t    1\n" "$PR" -t -2 tab
expect "tabs cut to the column" "a\tb\tc\td\te   1\n" "$PR" -t -2 tabs
# -s: the one character, no padding; the width 512
expect "-s:" "1:3\n2:4\n" "$PR" -t -2 -s: n4
L=$(printf 'x%.0s' $(seq 1 100))
printf '%s\ny\n' "$L" > l100
expect "-s width 512" "$L:y\n" "$PR" -t -2 -s: l100
expect "-s -w cuts" "aaaaaaaaa:bbbbbbbbb\n" "$PR" -t -2 -s: -w 19 long
expect "-s alone is a tab" "1\t3\n2\t4\n" "$PR" -t -2 -s n4
status "-column with -m" nonzero "$PR" -m -2 xy xy
status "too narrow" nonzero "$PR" -t -30 -w 20 xy
stderr_has "too narrow" "pr: "

# -m: a line of each file side by side, until all have ended.
expect "-m" "x\t\t\t\t    x\ny\t\t\t\t    y\n" "$PR" -t -m xy xy
expect "-m shorter file" "a\t\t\t\t    x\n\t\t\t\t    y\n" "$PR" -t -m one xy
expect "-m files end" "a\t\t\t\t    a\n" "$PR" -t -m one one
expect "-m -s" "x:a:x\ny::y\n" "$PR" -t -m -s: xy one xy
cases=$((cases + 1))
if ! timeout 10 "$PR" -m -l 5 one one > got || [ "$(cat got)" != "$(printf 'a\t\t\t\t    a')" ]; then
    echo "FAIL -m -l 5 ends"
    fail=1
fi

# -n: width digits (the last of them), then the separator
expect "-n" "    1\tx\n    2\ty\n" "$PR" -t -n xy
expect "-n3" "  1\tx\n  2\ty\n" "$PR" -t -n3 xy
expect "-n:3" "  1:x\n  2:y\n" "$PR" -t -n:3 xy
expect "-n:1 wraps" "0:10\n1:11\n2:12\n" sh -c "\"$PR\" -t -n:1 n12 | tail -3"
expect "-n each column" "  1:1\t    3:3\n  2:2\t    4:4\n" "$PR" -t -n:3 -2 -w 20 n4
expect "-m -n" "    1\tx\t\t\t\tx\n    2\ty\t\t\t\ty\n" "$PR" -t -m -n xy xy

# -d, -e, -i, -o
expect "-d" "x\n\ny\n\n" "$PR" -t -d xy
expect "-d keeps a line with its blank" \
    "\n\nJan  2 03:04 2020 n4 Page 1\n\n\n1\n\n2\n\n$(nl10 6)\n\nJan  2 03:04 2020 n4 Page 2\n\n\n3\n\n4\n\n$(nl10 6)" \
    "$PR" -d -l 15 n4
printf 'a\tb\n' > t1
printf 'axb\n' > t2
expect "-e" "a       b\n" "$PR" -t -e t1
expect "-e4" "a   b\n" "$PR" -t -e4 t1
expect "-ex4" "a   b\n" "$PR" -t -ex4 t2
printf 'a        b\n' > sp
expect "-i" "a\t b\n" "$PR" -t -i sp
expect "-i4" "a\t\t b\n" "$PR" -t -i4 sp
expect "-i -o" "  a\t   b\n" "$PR" -t -i -o 2 sp
expect "-o" "   x\n   y\n" "$PR" -t -o 3 xy
expect "-o header" "\n\n   Jan  2 03:04 2020 xy Page 1\n\n\n   x\n   y\n$(nl10 9)" "$PR" -o 3 -l 16 xy

# -F and -f: a form feed for the rest of the page and the trailer
expect "-F" "\n\nJan  2 03:04 2020 xy Page 1\n\n\nx\ny\n\f" "$PR" -F -l 16 xy
expect "-f not on a terminal" "\n\nJan  2 03:04 2020 xy Page 1\n\n\nx\ny\n\f" "$PR" -f -l 16 xy
# -p pauses only on a terminal
expect "-p not on a terminal" "x\ny\n" "$PR" -p -t xy

# +page
expect "+2" "\n\nJan  2 03:04 2020 n12 Page 2\n\n\n6\n7\n8\n9\n10\n$(nl10 5)\n\nJan  2 03:04 2020 n12 Page 3\n\n\n11\n12\n$(nl10 8)" \
    "$PR" +2 -l 15 n12
expect "+3 -t -n" "   11\t11\n   12\t12\n" "$PR" +3 -l 5 -t -n n12

# Option grammar: grouped letters, attached option-arguments
expect "-at -2" "1\t\t\t\t    2\n3\t\t\t\t    4\n" "$PR" -at -2 n4
expect "-tl 5" "1\n2\n3\n4\n" "$PR" -tl 5 n4
expect "-l15" "$(printf '%s' "$HDR1" | sed 's/one/n4/')1\n2\n3\n4\n$(nl10 6)" "$PR" -l15 n4
expect "-w20 -t2" "aaaaaaaaa bbbbbbbbb\n" "$PR" -w20 -t2 long
expect "-hX" "\n\nJan  2 03:04 2020 X Page 1\n\n\nx\ny\n$(nl10 9)" "$PR" -hX -l 16 xy
expect "--" "x\ny\n" "$PR" -t -- xy
expect "big -l" "x\ny\n" "$PR" -t -l 100001 xy
status "-l 0" nonzero "$PR" -l 0 xy
status "unknown option" nonzero "$PR" -z xy
stderr_has "unknown option" "pr: "

# Errors: diagnosed, and the other files still written
expect "missing file" "a\na\n" "$PR" -t one nosuch one
stderr_has "missing file" "pr: nosuch: "
status "missing file" nonzero "$PR" -t one nosuch one
mkdir dir
expect "directory" "a\na\n" "$PR" -t one dir one
stderr_has "directory" "pr: dir: "
expect "-r" "a\n" "$PR" -r -t one nosuch
cases=$((cases + 1))
if [ -s err ]; then
    echo "FAIL -r: [$(cat err)]"
    fail=1
fi
status "-r status" nonzero "$PR" -r -t one nosuch
if [ -w /dev/full ]; then
    status "write error" nonzero sh -c "\"$PR\" -t xy > /dev/full"
    stderr_has "write error" "pr: write error"
fi

# The input is not held: a page is written as it is read.
cases=$((cases + 1))
if [ "$(yes | timeout 10 "$PR" -t | head -1)" != y ]; then
    echo "FAIL streaming"
    fail=1
fi
cases=$((cases + 1))
if [ "$(yes | timeout 10 "$PR" -t -2 | head -1)" != "$(printf 'y\t\t\t\t    y')" ]; then
    echo "FAIL streaming columns"
    fail=1
fi

# Display widths, where a UTF-8 locale is available.
U=
for l in C.UTF-8 en_US.UTF-8 C.utf8 en_US.utf8; do
    if [ "$(LC_ALL=$l locale charmap 2>/dev/null)" = UTF-8 ]; then
        U=$l
        break
    fi
done
if [ -n "$U" ]; then
    printf '\350\241\250\nx\n' > wide
    expect "wide character" "\350\241\250  x\n" env LC_ALL="$U" "$PR" -t -2 -w 8 wide
    expect "-s multibyte" "x\303\251a\ny\n" env LC_ALL="$U" "$PR" -t -m -s$(printf '\303\251') xy one
else
    echo "skip: no UTF-8 locale"
fi

if [ $fail = 0 ]; then
    echo "ok ($cases cases)"
fi
exit $fail
