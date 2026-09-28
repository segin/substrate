#!/bin/sh
#
# run_tests.sh - behaviour tests for mktemp(1).
#
#   sh run_tests.sh [path/to/mktemp]
#
# Works in a scratch directory used as $TMPDIR.  Prints "ok"/"FAIL" per
# case and a final "Result:" line; exits non-zero if any case failed.

HERE=$(cd "$(dirname "$0")" && pwd)
MKTEMP=${1:-$HERE/../../../usr.bin/mktemp/mktemp}
case $MKTEMP in /*) ;; *) MKTEMP=$(pwd)/$MKTEMP ;; esac

[ -x "$MKTEMP" ] || { echo "run_tests.sh: no mktemp at $MKTEMP" >&2; exit 2; }

WORK=${TMPDIR:-/tmp}/mktemp-test.$$
rm -rf "$WORK"
mkdir "$WORK" || exit 2
trap 'rm -rf "$WORK"' EXIT INT TERM
cd "$WORK" || exit 2
mkdir tmp other

passed=0
failed=0

ok()   { echo "ok: $1"; passed=$((passed + 1)); }
fail() { echo "FAIL: $1"; failed=$((failed + 1)); }

# The permission bits of a file, as ls prints them (e.g. -rw-------).
perms() { ls -ld "$1" | cut -c1-10; }

TMPDIR=$WORK/tmp
export TMPDIR

f=$("$MKTEMP")
case $f in
"$WORK/tmp/tmp."??????????)
    if [ -f "$f" ] && [ "$(perms "$f")" = "-rw-------" ]; then
        ok "default: tmp.XXXXXXXXXX file, mode 0600, in \$TMPDIR"
    else
        fail "default: $f is not a mode-0600 file"
    fi ;;
*)  fail "default: unexpected name '$f'" ;;
esac

d=$("$MKTEMP" -d)
if [ -d "$d" ] && [ "$(perms "$d")" = "drwx------" ]; then
    ok "-d: directory, mode 0700"
else
    fail "-d: $d is not a mode-0700 directory"
fi

f=$("$MKTEMP" local.XXXXXX)
case $f in
local.??????) [ -f "$f" ] && ok "template: relative to the current directory" ||
                  fail "template: $f not created" ;;
*)            fail "template: unexpected name '$f'" ;;
esac

f=$("$MKTEMP" "$WORK/other/abs.XXX")
case $f in
"$WORK/other/abs."???) [ -f "$f" ] && ok "template: absolute path, three X's" ||
                          fail "absolute template: $f not created" ;;
*)                     fail "absolute template: unexpected name '$f'" ;;
esac

f=$("$MKTEMP" -t t.XXXXXX)
case $f in "$WORK/tmp/t."??????) ok "-t: in \$TMPDIR" ;;
           *) fail "-t: unexpected name '$f'" ;; esac

f=$(TMPDIR= "$MKTEMP" -t -p "$WORK/other" t.XXXXXX)
case $f in "$WORK/other/t."??????) ok "-t -p: -p's directory without \$TMPDIR" ;;
           *) fail "-t -p: unexpected name '$f'" ;; esac

f=$("$MKTEMP" -p "$WORK/other" p.XXXXXX)
case $f in "$WORK/other/p."??????) ok "-p dir: in dir" ;;
           *) fail "-p dir: unexpected name '$f'" ;; esac

f=$("$MKTEMP" -u u.XXXXXX)
case $f in
u.??????) [ ! -e "$f" ] && ok "-u: prints a name, creates nothing" ||
              fail "-u: $f was created" ;;
*)        fail "-u: unexpected name '$f'" ;;
esac

a=$("$MKTEMP" same.XXXXXX)
b=$("$MKTEMP" same.XXXXXX)
if [ "$a" != "$b" ] && [ -f "$a" ] && [ -f "$b" ]; then
    ok "two runs, two different files"
else
    fail "two runs gave '$a' and '$b'"
fi

out=$("$MKTEMP" few.XX 2>&1)
if [ $? -eq 1 ] && [ ! -e few.XX ] && echo "$out" | grep -q "too few X's"; then
    ok "fewer than three X's: error"
else
    fail "fewer than three X's: expected an error, got '$out'"
fi

out=$("$MKTEMP" -q few.XX 2>&1)
if [ $? -eq 1 ] && [ -z "$out" ]; then
    ok "-q: fails silently"
else
    fail "-q: expected silence and status 1, got '$out'"
fi

"$MKTEMP" -t a/b.XXXXXX > /dev/null 2>&1
[ $? -eq 1 ] && ok "-t with a / in the template: error" ||
    fail "-t with a / in the template: expected an error"

"$MKTEMP" "$WORK/missing/x.XXXXXX" > /dev/null 2>&1
[ $? -eq 1 ] && ok "missing directory: error" ||
    fail "missing directory: expected an error"

"$MKTEMP" -z > /dev/null 2>&1
[ $? -eq 1 ] && ok "invalid option: error" || fail "invalid option: expected an error"

echo
echo "Result: $([ $failed -eq 0 ] && echo PASS || echo FAIL) ($passed passed, $failed failed)"
[ $failed -eq 0 ]
