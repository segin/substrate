#!/bin/sh
#
# run_tests.sh - behaviour tests for env(1).
#
#   sh run_tests.sh [path/to/env]
#
# Prints "ok"/"FAIL" per case and a final "Result:" line; exits non-zero
# if any case failed.

HERE=$(cd "$(dirname "$0")" && pwd)
ENV=${1:-$HERE/../../../usr.bin/env/env}
case $ENV in /*) ;; *) ENV=$(pwd)/$ENV ;; esac

[ -x "$ENV" ] || { echo "run_tests.sh: no env at $ENV" >&2; exit 2; }

passed=0
failed=0

# check NAME EXPECTED ACTUAL
check() {
    if [ "$3" = "$2" ]; then
        echo "ok: $1"
        passed=$((passed + 1))
    else
        echo "FAIL: $1: expected '$2', got '$3'"
        failed=$((failed + 1))
    fi
}

check "no utility: prints the environment" \
    "SLEXT_A=1" "$(SLEXT_A=1 "$ENV" | grep '^SLEXT_A=')"

check "-i: empty environment" "" "$("$ENV" -i)"

check "- is -i" "X=1" "$("$ENV" - X=1)"

check "assignments, later ones win" "A=2
B=3" "$("$ENV" -i A=1 B=3 A=2 | sort)"

check "-u removes a variable" "" "$(SLEXT_A=1 "$ENV" -u SLEXT_A | grep '^SLEXT_A=')"

check "-uNAME form" "" "$(SLEXT_A=1 "$ENV" -uSLEXT_A | grep '^SLEXT_A=')"

check "-- ends options" "X=1" "$("$ENV" -i -- X=1)"

check "runs utility in the new environment" "bar" \
    "$("$ENV" FOO=bar sh -c 'echo $FOO')"

check "arguments reach utility unchanged" "a b=c -i" \
    "$("$ENV" sh -c 'echo "$@"' sh a b=c -i)"

# With -i there is no PATH; execvp falls back to /bin:/usr/bin.
check "-i with a bare command name" "ran" "$("$ENV" -i sh -c 'echo ran')"

script=${TMPDIR:-/tmp}/env-test.$$
printf '#!%s sh\necho "shebang $1"\n' "$ENV" > "$script"
chmod +x "$script"
check "#!env interpreter line" "shebang arg" "$("$script" arg)"
rm -f "$script"

"$ENV" sh -c 'exit 3'
check "utility's exit status" "3" "$?"

"$ENV" slext-no-such-command 2> /dev/null
check "utility not found: 127" "127" "$?"

"$ENV" / 2> /dev/null
check "utility not runnable: 126" "126" "$?"

"$ENV" -z 2> /dev/null
check "invalid option: 125" "125" "$?"

"$ENV" -u 2> /dev/null
check "-u without a name: 125" "125" "$?"

"$ENV" '=x' 2> /dev/null
check "empty name in assignment: 125" "125" "$?"

echo
echo "Result: $([ $failed -eq 0 ] && echo PASS || echo FAIL) ($passed passed, $failed failed)"
[ $failed -eq 0 ]
