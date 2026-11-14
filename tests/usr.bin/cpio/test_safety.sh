#!/usr/bin/env bash
# Safe extraction, the default: a member named with ".." or an absolute
# path is refused and nothing is written outside the directory; with
# --absolute-paths an absolute name is allowed, with --insecure both.
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CPIO=${CPIO:-"$ROOT/usr.bin/cpio/cpio"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
fail=0

mkdir -p in/sub out abs
printf 'escape\n' > in/escape
printf 'abs\n' > abs/file
# "../escape" from in/sub, and an absolute name, then a plain member
(cd in/sub && printf '../escape\n%s\n' "$TMP/abs/file" | "$CPIO" -o --quiet) > a.cpio
rm in/escape abs/file

check() {   # check LABEL WANT-ESCAPE WANT-ABS
    local got_e=no got_a=no
    [ -e in/escape ] && got_e=yes
    [ -e abs/file ] && got_a=yes
    if [ "$got_e $got_a" != "$2 $3" ]; then
        echo "FAIL $1: ../escape written: $got_e (want $2), absolute written: $got_a (want $3)"
        fail=1
    fi
    rm -f in/escape abs/file
}

(cd in/sub && "$CPIO" -id --quiet < "$TMP/a.cpio" 2>/dev/null)
check "default" no no
(cd in/sub && "$CPIO" -id --quiet --safe-extract < "$TMP/a.cpio" 2>/dev/null)
check "--safe-extract" no no
(cd in/sub && "$CPIO" -id --quiet --absolute-paths < "$TMP/a.cpio" 2>/dev/null)
check "--absolute-paths" no yes
(cd in/sub && "$CPIO" -id --quiet --insecure < "$TMP/a.cpio" 2>/dev/null)
check "--insecure" yes yes

# refused members are reported, and the run says so in its status
out=$(cd in/sub && "$CPIO" -id --quiet < "$TMP/a.cpio" 2>&1)
rc=$?
want="cpio: ../escape: unsafe name, not extracted
cpio: $TMP/abs/file: unsafe name, not extracted"
if [ "$out" != "$want" ] || [ "$rc" -ne 1 ]; then
    printf 'FAIL refusal report (status %s):\n%s\n' "$rc" "$out"
    fail=1
fi
exit $fail
