#!/bin/sh
# The options that do not choose what is assembled: -msyntax, -al,
# --statistics, --target-help and -c.  Each is held to what it does, and
# to doing nothing else -- the object is the one made without it, byte
# for byte, where the option is not meant to change the code.
#
# --defsym and the warning options are test_cli_defsym.sh and
# test_cli_warnings.sh.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
bad() { echo "FAIL: $*"; fail=1; }

# text OBJECT: its .text, in hex.
text() {
    objcopy -O binary --only-section=.text "$1" text.bin 2>/dev/null || return 1
    od -An -v -tx1 text.bin | tr -d ' \n'
}

cat > att.s <<'SRC'
	.text
	.globl _start
_start:
	mov $1, %eax
	mov (%ebx), %ecx
	ret
SRC
# The same three instructions as Intel writes them, with nothing in the
# source to say so: only the option can.
cat > intel.s <<'SRC'
	.text
	.globl _start
_start:
	mov eax, 1
	mov ecx, [ebx]
	ret
SRC
CODE=b8010000008b0bc3

"$AS" -32 -o plain.o att.s > out 2>&1 || bad "the reference source is refused: $(head -1 out)"
[ "$(text plain.o)" = "$CODE" ] || bad "the reference source assembles to '$(text plain.o)', not '$CODE'"
[ -s out ] && bad "the reference source says something: $(head -1 out)"

# same WHAT OBJECT: OBJECT is plain.o.
same() {
    cmp -s plain.o "$2" || bad "$1: the object differs from the one made without it"
}

# --- -msyntax ----------------------------------------------------------
rm -f t.o
"$AS" -32 -msyntax=att -o t.o att.s > out 2>&1 || bad "-msyntax=att: refused: $(head -1 out)"
same "-msyntax=att" t.o

for form in "-msyntax=intel" "-msyntax intel"; do
    rm -f t.o
    # shellcheck disable=SC2086
    "$AS" -32 $form -o t.o intel.s > out 2>&1 || bad "$form: refused: $(head -1 out)"
    [ "$(text t.o)" = "$CODE" ] || bad "$form: Intel source assembles to '$(text t.o)', not '$CODE'"
done

# Without the option the Intel source is not those instructions: were it,
# the two runs above would have shown nothing.
rm -f t.o
if "$AS" -32 -o t.o intel.s > out 2>&1 && [ "$(text t.o)" = "$CODE" ]; then
    bad "Intel source assembles as Intel without -msyntax=intel"
fi

# A directive in the source outlasts the option.
{ printf '\t.att_syntax\n'; cat att.s; } > att-directive.s
rm -f t.o
"$AS" -32 -msyntax=intel -o t.o att-directive.s > out 2>&1 ||
    bad "-msyntax=intel with .att_syntax in the source: refused: $(head -1 out)"
[ "$(text t.o)" = "$CODE" ] || bad "-msyntax=intel with .att_syntax: '$(text t.o)', not '$CODE'"

# A syntax there is not, and none named: refused as a misuse, with no
# object.
for form in "-msyntax=bogus" "-msyntax="; do
    rm -f t.o
    "$AS" -32 "$form" -o t.o att.s > out 2>&1
    rc=$?
    [ "$rc" -eq 2 ] || bad "$form: exit $rc, not 2"
    [ -e t.o ] && bad "$form: an object was written"
    grep -q 'msyntax' out || bad "$form: the message does not name the option: $(head -1 out)"
done
rm -f t.o
"$AS" -32 att.s -o t.o -msyntax > out 2>&1
[ $? -eq 2 ] || bad "-msyntax with no argument is not refused as a misuse"

# --- -al ---------------------------------------------------------------
# A listing has a line for each line of the source, numbered, in order,
# with the source's text.
listing_is_of() {
    n=0
    while IFS= read -r src; do
        n=$((n + 1))
        line=$(sed -n "${n}p" "$1")
        case $line in
        *"$src") ;;
        *) bad "$3: line $n of the listing is '$line', the source's is '$src'"; return ;;
        esac
        num=$(printf '%s\n' "$line" | awk '{ print $1 }')
        [ "$num" = "$n" ] || { bad "$3: line $n of the listing is numbered '$num'"; return; }
    done < "$2"
    [ "$(wc -l < "$1")" -eq "$n" ] || bad "$3: the listing has $(wc -l < "$1") lines for a source of $n"
}

rm -f t.o named.lst
"$AS" -32 -al=named.lst -o t.o att.s > out 2>&1 || bad "-al=FILE: refused: $(head -1 out)"
same "-al=FILE" t.o
if [ -f named.lst ]; then listing_is_of named.lst att.s "-al=FILE"; else bad "-al=FILE: no listing written"; fi
[ -e t.o.lst ] && bad "-al=FILE: a second listing was written beside the object"

# Without a name it goes beside the object.
rm -f t.o t.o.lst
"$AS" -32 -al -o t.o att.s > out 2>&1 || bad "-al: refused: $(head -1 out)"
same "-al" t.o
if [ -f t.o.lst ]; then listing_is_of t.o.lst att.s "-al"; else bad "-al: no listing beside the object"; fi

# No listing unless asked for.
rm -f t.o t.o.lst
"$AS" -32 -o t.o att.s > /dev/null 2>&1
[ -e t.o.lst ] && bad "a listing is written when none is asked for"

# One that cannot be written is an error.
rm -f t.o
"$AS" -32 -al="$work/no/such/dir/x.lst" -o t.o att.s > out 2>&1
[ $? -eq 1 ] || bad "-al=FILE in a directory there is not: not an error"
grep -q 'x\.lst' out || bad "-al=FILE unwritable: the message does not name the file: $(head -1 out)"

# --- --statistics -------------------------------------------------------
# Lines on the standard error, each `as: statistics:` and then name=value
# pairs with numbers for values; the last is the whole run.  Nothing on
# the standard output, and the object untouched.
rm -f t.o
"$AS" -32 --statistics -o t.o att.s > stats.out 2> stats.err || bad "--statistics: refused"
same "--statistics" t.o
[ -s stats.out ] && bad "--statistics: writes to the standard output"
[ -s stats.err ] || bad "--statistics: says nothing"
if grep -qv '^as: statistics:\( [a-z_]*=[a-z0-9-]*\)\{1,\}$' stats.err; then
    bad "--statistics: a line is not statistics: $(grep -v '^as: statistics:\( [a-z_]*=[a-z0-9-]*\)\{1,\}$' stats.err | head -1)"
fi
tail -1 stats.err | grep -q '^as: statistics: wall_us=[0-9][0-9]*\( [a-z_]*=[0-9][0-9]*\)*$' ||
    bad "--statistics: the last line is not the run's totals: $(tail -1 stats.err)"

# --- --target-help ------------------------------------------------------
# Prints and stops: exit 0, text on the standard output only, and no
# object even when a source and an output are named.  It names the two
# x86 targets and the option that chooses the syntax.
rm -f t.o
"$AS" --target-help > help.out 2> help.err
[ $? -eq 0 ] || bad "--target-help: does not exit 0"
[ -s help.out ] || bad "--target-help: prints nothing"
[ -s help.err ] && bad "--target-help: writes to the standard error"
for word in i386 x86-64 msyntax; do
    grep -q -- "$word" help.out || bad "--target-help: does not mention $word"
done
"$AS" --target-help -32 -o t.o att.s > help2.out 2>&1
[ $? -eq 0 ] || bad "--target-help with a source: does not exit 0"
[ -e t.o ] && bad "--target-help with a source: assembles it"
cmp -s help.out help2.out || bad "--target-help with a source: prints something else"

# --- -c -----------------------------------------------------------------
# A compiler driver's option, accepted so that the assembler can stand in
# for one; it changes nothing.
rm -f t.o
"$AS" -32 -c -o t.o att.s > out 2>&1 || bad "-c: refused: $(head -1 out)"
same "-c" t.o
[ -s out ] && bad "-c: says something: $(head -1 out)"

[ "$fail" -eq 0 ] && echo "ok: -msyntax, -al, --statistics, --target-help, -c"
exit "$fail"
