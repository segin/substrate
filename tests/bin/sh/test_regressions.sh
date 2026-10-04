#!/bin/sh

SH="${SH:-./sh_host}"
TMPDIR=$(mktemp -d)
FAIL=0

check_eq() {
    name="$1"
    got="$2"
    expected="$3"

    if [ "$got" = "$expected" ]; then
        echo "PASS: $name"
    else
        echo "FAIL: $name: expected '$expected', got '$got'"
        FAIL=1
    fi
}

check_status_nonzero() {
    name="$1"
    shift
    "$@"
    status=$?
    if [ "$status" -ne 0 ]; then
        echo "PASS: $name"
    else
        echo "FAIL: $name: expected nonzero status"
        FAIL=1
    fi
}

OUT=$($SH -c 'TMP=outer; f(){ export OBSERVED=$TMP; }; TMP=inner f; printf "%s|%s" "$TMP" "$OBSERVED"')
check_eq "function temp assignment" "$OUT" "outer|inner"

OUT=$($SH -c 'unset TMP; TMP=inner true; printf "%s" "${TMP-unset}"')
check_eq "builtin temp assignment" "$OUT" "unset"

printf 'false\n' > "$TMPDIR/source.sh"
$SH -c ". $TMPDIR/source.sh"
check_eq "source status" "$?" "1"

OUT=$($SH -c 'X=$(false); printf "%s" "$?"')
check_eq "assignment-only cmdsub status" "$OUT" "1"

check_status_nonzero "syntax error status" \
    "$SH" -c 'case x in a) echo hi ;;'

ERR=$($SH -c 'unset MISSING; echo ${MISSING:?boom}' 2>&1 >/dev/null)
STATUS=$?
if [ "$STATUS" -ne 0 ] && echo "$ERR" | grep -q "boom"; then
    echo "PASS: parameter error status"
else
    echo "FAIL: parameter error status: status=$STATUS err='$ERR'"
    FAIL=1
fi

$SH -c "(echo hi) > $TMPDIR/subshell.out"
if [ "$(cat "$TMPDIR/subshell.out")" = "hi" ]; then
    echo "PASS: subshell redirection"
else
    echo "FAIL: subshell redirection"
    FAIL=1
fi

# "!" negates a pipeline's status.  It used to be run as a command named
# "!", which is not found: status 127, so "if ! cmd" always took the else
# branch.
check_eq "! false in if" \
    "$($SH -c 'if ! false; then echo yes; else echo no; fi')" "yes"
check_eq "! true in if" \
    "$($SH -c 'if ! true; then echo yes; else echo no; fi')" "no"
check_eq "! status" \
    "$($SH -c '! true; echo $?; ! false; echo $?' | tr '\n' ' ')" "1 0 "
check_eq "! negates a whole pipeline" \
    "$($SH -c 'if ! echo abc | grep -q zzz; then echo yes; else echo no; fi')" "yes"
check_eq "! is an ordinary word elsewhere" \
    "$($SH -c 'echo ! "!"; [ ! -e /nonexistent ] && echo ok' | tr '\n' ' ')" "! ! ok "
check_eq "! pipeline is exempt from set -e" \
    "$($SH -c 'set -e; ! true; ! false; echo alive')" "alive"

rm -rf "$TMPDIR"

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
