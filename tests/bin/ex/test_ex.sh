#!/bin/bash
set -e

EX_BIN="./bin/ex/ex"
make -C bin/ex NATIVE_BUILD=1 >/dev/null

TEST_FAILS=0
TEST_PASS=0

# Runs a batch script through ex, nvi, and vim -es and compares the output buffer.
run_test() {
    local name="$1"
    local init_text="$2"
    local script="$3"
    
    # Create temporary files
    local f_ex=$(mktemp)
    local f_nvi=$(mktemp)
    local f_vim=$(mktemp)
    
    echo -e "$init_text" > "$f_ex"
    echo -e "$init_text" > "$f_nvi"
    echo -e "$init_text" > "$f_vim"

    # Run ex (our implementation)
    echo -e "$script" | "$EX_BIN" "$f_ex" >/dev/null 2>&1 || true

    # Run nvi (if installed, to serve as BSD reference oracle)
    if command -v nvi >/dev/null 2>&1; then
        echo -e "$script" | nvi "$f_nvi" >/dev/null 2>&1 || true
        if ! diff -u "$f_nvi" "$f_ex" > /dev/null; then
            echo "FAIL: $name (Differs from nvi)"
            diff -u "$f_nvi" "$f_ex"
            TEST_FAILS=$((TEST_FAILS+1))
            rm -f "$f_ex" "$f_nvi" "$f_vim"
            return
        fi
    fi

    # Run vim (if installed, to serve as Vim reference oracle)
    if command -v vim >/dev/null 2>&1; then
        echo -e "$script" | vim -es "$f_vim" >/dev/null 2>&1 || true
        if ! diff -u "$f_vim" "$f_ex" > /dev/null; then
            echo "FAIL: $name (Differs from vim -es)"
            diff -u "$f_vim" "$f_ex"
            TEST_FAILS=$((TEST_FAILS+1))
            rm -f "$f_ex" "$f_nvi" "$f_vim"
            return
        fi
    fi

    echo "PASS: $name"
    TEST_PASS=$((TEST_PASS+1))
    rm -f "$f_ex" "$f_nvi" "$f_vim"
}

# Wait, run_test currently redirects strictly the file. Let's redirect standard output to /dev/null to avoid spam.
run_test "Append and delete" "line1\nline2\nline3" "2d\nwq\n"

# Core commands
run_test "Insert" "line1\nline2" "1i\ninserted\n.\nwq\n"
run_test "Append" "line1\nline2" "1a\nappended\n.\nwq\n"
run_test "Change" "line1\nline2\nline3" "1,2c\nchanged\n.\nwq\n"

# Copy and Move
run_test "Copy (t)" "line1\nline2\nline3" "1,2t$\nwq\n"
run_test "Move (m)" "line1\nline2\nline3" "1m$\nwq\n"

# Join
run_test "Join (j)" "line1\nline2\nline3" "1,2j\nwq\n"

# Yank and Put
run_test "Yank and Put (unnamed)" "line1\nline2\nline3" "1y\n$pu\nwq\n"
run_test "Yank and Put (named a)" "line1\nline2\nline3" "2ya a\n$pu a\nwq\n"

# Substitute
run_test "Substitute basic" "hello world\nfoo bar" "1s/world/universe/\nwq\n"
run_test "Substitute global flag" "foo foo foo\nbar" "1s/foo/baz/g\nwq\n"

# Global and v commands
run_test "Global print (state doesn't change file)" "foo\nbar\nfoo" "g/foo/p\nwq\n"
run_test "Global delete" "foo\nbar\nfoo\nbaz" "g/foo/d\nwq\n"
run_test "Inverse global delete (v)" "foo\nbar\nfoo\nbaz" "v/foo/d\nwq\n"

echo "$TEST_PASS tests run."
exit $((TEST_FAILS > 0 ? 1 : 0))
