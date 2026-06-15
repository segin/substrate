#!/bin/bash
# test_xargs.sh — oracle-comparison harness: substrate xargs vs system xargs.
# Cases are chosen where POSIX/GNU/BSD agree so the system xargs is a valid
# oracle; BSD-specific behavior (-J) and exit codes are asserted directly.
set -u

XBIN="./bin/xargs/xargs"
SYS="$(command -v xargs)"
make -C bin/xargs NATIVE_BUILD=1 >/dev/null 2>&1 || { echo "build failed"; exit 1; }
[ -x "$XBIN" ] || { echo "no $XBIN"; exit 1; }
[ -n "$SYS" ] || { echo "no system xargs to compare against"; exit 1; }

PASS=0 FAIL=0

# oracle <name> <input> <args...> : compare stdout of ours vs system
oracle() {
    local name="$1"; local input="$2"; shift 2
    local a b
    a=$(printf '%b' "$input" | "$XBIN" "$@" 2>/dev/null)
    b=$(printf '%b' "$input" | "$SYS" "$@" 2>/dev/null)
    if [ "$a" = "$b" ]; then PASS=$((PASS+1));
    else FAIL=$((FAIL+1)); echo "FAIL[oracle] $name"; echo "  ours: $a"; echo "  sys : $b"; fi
}

# expect <name> <got> <want>
expect() {
    local name="$1" got="$2" want="$3"
    if [ "$got" = "$want" ]; then PASS=$((PASS+1));
    else FAIL=$((FAIL+1)); echo "FAIL[expect] $name: got [$got] want [$want]"; fi
}

# --- oracle cases (GNU/BSD agree) ---
oracle basic        'a b c\n'              echo
oracle n1           'a b c\n'              -n1 echo
oracle n2           'a b c d e\n'          -n2 echo
oracle L2           '1\n2\n3\n4\n5\n'      -L2 echo
oracle null0        'a b\0c d\0'           -0 echo
oracle delim        'a,b,c'                -d, echo
oracle insert       'one\ntwo\n'           -I{} echo "<{}>"
oracle eof          'a\nb\nSTOP\nc\n'      -E STOP echo
oracle quotes       "'a b' c\n"            -n1 echo
oracle backslash    'a\\ b c\n'            -n1 echo
oracle empty_norun  ''                     -r echo X
oracle empty_run    ''                     echo X
oracle smalls       'aa bb cc dd\n'        -s 12 echo

# --- direct assertions (BSD-specific / exit codes) ---
expect Jinsert "$(printf 'a\nb\nc\n' | "$XBIN" -J% echo pre % post)" "pre a b c post"
printf 'x\n' | "$XBIN" sh -c 'exit 1' >/dev/null 2>&1; expect rc123 "$?" "123"
printf 'x\n' | "$XBIN" sh -c 'exit 0' >/dev/null 2>&1; expect rc0   "$?" "0"
printf 'x\n' | "$XBIN" /nonexistent-cmd-zzz >/dev/null 2>&1; expect rc127 "$?" "127"
expect trace "$(printf 'p q\n' | "$XBIN" -t echo 2>&1 1>/dev/null)" "echo p q"

echo "-----"
echo "xargs: PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
