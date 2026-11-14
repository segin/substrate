#!/usr/bin/env bash
# cpio(1), case by case: exact standard output, standard error and exit
# status, with GNU cpio as the judge where POSIX is silent.  GNU cpio, when
# installed, is also the other end of the interoperability cases.
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CPIO=${CPIO:-"$ROOT/usr.bin/cpio/cpio"}
TMP=$(mktemp -d)
trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
export TZ=UTC LC_ALL=C
umask 022

GNU=
if command -v cpio >/dev/null 2>&1 && cpio --version 2>/dev/null | grep -q GNU; then
    GNU=$(command -v cpio)
fi

fail=0
cases=0

# run CMD...: run it in a subshell, keeping stdout, stderr and status.
run() {
    "$@" >"$TMP/.out" 2>"$TMP/.err"
    rc=$?
    out=$(cat "$TMP/.out")
    err=$(cat "$TMP/.err")
}

# ok LABEL COND...: COND (a test(1) expression or command) holds.
ok() {
    label=$1; shift
    cases=$((cases + 1))
    if ! "$@"; then
        echo "FAIL $label"
        fail=1
    fi
}

# same LABEL WANT GOT
same() {
    cases=$((cases + 1))
    if [ "$2" != "$3" ]; then
        printf 'FAIL %s\n  want: %s\n  got:  %s\n' "$1" "$2" "$3"
        fail=1
    fi
}

# fresh DIR: an empty directory
fresh() { rm -rf "$1"; mkdir -p "$1"; }

# ---- source tree used by most cases
mkdir src src/d
printf 'hello\n' > src/f
printf 'x\n' > src/d/g

# ---- diagnostics
run "$CPIO" -x
same "unknown option" "cpio: unknown option -x" "$(head -1 .err)"
same "unknown option status" 2 "$rc"
run "$CPIO" -o -H
same "missing argument" "cpio: option -H needs an argument" "$(head -1 .err)"
run "$CPIO" -o -H zip </dev/null
same "unknown format" "cpio: unknown archive format: zip" "$err"
run "$CPIO" -i -F nonexistent.cpio
same "missing archive" "cpio: nonexistent.cpio: No such file or directory" "$err"
same "missing archive status" 2 "$rc"
printf 'src/f\n' | "$CPIO" -o -F v.cpio
fresh xv
run sh -c "cd xv && \"$CPIO\" -idv -F ../v.cpio"
same "-iv names the member, not ./member" "src/f" "$err"

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "ok ($cases cases)"
