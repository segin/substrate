#!/bin/bash
set -ea

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"

TIME_BIN="$REPO_ROOT/bin/time/time"
TMP_DIR="$(mktemp -d -t test_time_XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

assert_exit_code() {
    local cmd="$1"
    local exp="$2"
    local msg="$3"
    set +e
    eval "$cmd" >/dev/null 2>&1
    local got=$?
    set -e
    if [[ "$got" != "$exp" ]]; then
        fail "$msg (got=$got exp=$exp)"
    fi
}

echo "Building time..."
make -C "$REPO_ROOT/bin/time" NATIVE_BUILD=1 >/dev/null

echo "--- Exit Code Logic ---"
# TIME-REQ-033: If utility invoked, time exits with utility's exit status
assert_exit_code "$TIME_BIN sh -c 'exit 7'" 7 "exit code pass-through"
# TIME-REQ-030: utility cannot be found -> 127
assert_exit_code "$TIME_BIN definitely-not-a-command" 127 "command not found"
# TIME-REQ-031: utility found but cannot be invoked -> 126
touch "$TMP_DIR/notexec"
chmod -x "$TMP_DIR/notexec"
assert_exit_code "$TIME_BIN $TMP_DIR/notexec" 126 "command not executable"

echo "--- POSIX Portable Output ---"
# TIME-REQ-020: portable output has 3 lines: real, user, sys
out=$("$TIME_BIN" -p true 2>&1)
lines=$(echo "$out" | wc -l)
if [[ "$lines" -ne 3 ]]; then
    fail "POSIX output must be 3 lines, got $lines"
fi
if ! echo "$out" | grep -q "^real "; then fail "POSIX output missing 'real '"; fi
if ! echo "$out" | grep -q "^user "; then fail "POSIX output missing 'user '"; fi
if ! echo "$out" | grep -q "^sys "; then fail "POSIX output missing 'sys '"; fi

echo "--- Output Routing ---"
# TIME-REQ-011: -o file writes to file, not stderr
"$TIME_BIN" -o "$TMP_DIR/out.txt" true 2> "$TMP_DIR/err.txt"
if [[ -s "$TMP_DIR/err.txt" ]]; then
    fail "stderr should be empty with -o"
fi
if [[ ! -s "$TMP_DIR/out.txt" ]]; then
    fail "output file should not be empty with -o"
fi

# TIME-REQ-012: -a appends to file
"$TIME_BIN" -o "$TMP_DIR/out.txt" -a true
lines=$(cat "$TMP_DIR/out.txt" | wc -l)
# BSD default single-line output -> 2 runs means 2 lines
if [[ "$lines" -ne 2 ]]; then
    fail "append mode should append to file, got $lines lines instead of 2"
fi

echo "--- BSD Extensions ---"
# TIME-REQ-050: -l includes rusage data
out=$("$TIME_BIN" -l true 2>&1)
if ! echo "$out" | grep -q "page faults"; then
    fail "BSD -l missing rusage metrics"
fi

echo "--- JSON Output (Substrate Extension) ---"
out=$("$TIME_BIN" --json sh -c "exit 42" 2>&1)
if ! echo "$out" | grep -q "\"exit_status\": 42"; then
    fail "JSON output missing or incorrect exit status"
fi
if ! echo "$out" | grep -q "^}$"; then
    fail "JSON output not properly formatted"
fi

echo "All time tests passed."
