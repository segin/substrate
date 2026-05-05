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

rm -rf "$TMPDIR"

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
