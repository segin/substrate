#!/bin/bash
set -eu

EX_BIN="${1:-./bin/ex/ex}"

TEST_FAILS=0
TEST_PASS=0

pass() {
    echo "PASS: $1"
    TEST_PASS=$((TEST_PASS + 1))
}

fail() {
    echo "FAIL: $1"
    TEST_FAILS=$((TEST_FAILS + 1))
}

run_oracle_test() {
    local name="$1"
    local init_text="$2"
    local script="$3"
    local f_ex
    local f_nvi
    local f_vim

    f_ex=$(mktemp)
    f_nvi=$(mktemp)
    f_vim=$(mktemp)

    printf "%b" "$init_text" >"$f_ex"
    printf "%b" "$init_text" >"$f_nvi"
    printf "%b" "$init_text" >"$f_vim"

    printf "%b" "$script" | "$EX_BIN" "$f_ex" >/dev/null 2>&1 || true

    if command -v nvi >/dev/null 2>&1; then
        printf "%b" "$script" | nvi "$f_nvi" >/dev/null 2>&1 || true
        if ! diff -u "$f_nvi" "$f_ex" >/dev/null; then
            fail "$name (differs from nvi)"
            diff -u "$f_nvi" "$f_ex" || true
            rm -f "$f_ex" "$f_nvi" "$f_vim"
            return
        fi
    fi

    if command -v vim >/dev/null 2>&1; then
        printf "%b" "$script" | vim -es "$f_vim" >/dev/null 2>&1 || true
        if ! diff -u "$f_vim" "$f_ex" >/dev/null; then
            fail "$name (differs from vim -es)"
            diff -u "$f_vim" "$f_ex" || true
            rm -f "$f_ex" "$f_nvi" "$f_vim"
            return
        fi
    fi

    pass "$name"
    rm -f "$f_ex" "$f_nvi" "$f_vim"
}

run_stdout_test() {
    local name="$1"
    local init_text="$2"
    local script="$3"
    local expected="$4"
    local file
    local output

    file=$(mktemp)
    printf "%b" "$init_text" >"$file"
    output=$(printf "%b" "$script" | "$EX_BIN" -s "$file" 2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -f "$file"
        return
    fi

    pass "$name"
    rm -f "$file"
}

run_stdout_with_auxfile_test() {
    local name="$1"
    local init_text="$2"
    local aux_text="$3"
    local script_template="$4"
    local expected="$5"
    local file
    local aux_file
    local script
    local output

    file=$(mktemp)
    aux_file=$(mktemp)
    printf "%b" "$init_text" >"$file"
    printf "%b" "$aux_text" >"$aux_file"
    script=${script_template//__READ_FILE__/$aux_file}
    output=$(printf "%b" "$script" | "$EX_BIN" -s "$file" 2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -f "$file" "$aux_file"
        return
    fi

    pass "$name"
    rm -f "$file" "$aux_file"
}

run_stderr_status_test() {
    local name="$1"
    local init_text="$2"
    local script="$3"
    local expected_status="$4"
    local expected_stderr="$5"
    local file
    local stdout_file
    local stderr_file
    local status
    local stderr_text

    file=$(mktemp)
    stdout_file=$(mktemp)
    stderr_file=$(mktemp)
    printf "%b" "$init_text" >"$file"

    set +e
    printf "%b" "$script" | "$EX_BIN" -s "$file" >"$stdout_file" 2>"$stderr_file"
    status=$?
    set -e

    stderr_text=$(cat "$stderr_file")
    if [ "$status" -ne "$expected_status" ] || [ "$stderr_text" != "$expected_stderr" ]; then
        fail "$name"
        printf 'expected status=%s stderr=%s\nactual status=%s stderr=%s\n' \
            "$expected_status" "$expected_stderr" "$status" "$stderr_text"
        rm -f "$file" "$stdout_file" "$stderr_file"
        return
    fi

    pass "$name"
    rm -f "$file" "$stdout_file" "$stderr_file"
}

run_oracle_test "Append and delete" "line1\nline2\nline3\n" "2d\nwq\n"
run_oracle_test "Insert" "line1\nline2\n" "1i\ninserted\n.\nwq\n"
run_oracle_test "Append" "line1\nline2\n" "1a\nappended\n.\nwq\n"
run_oracle_test "Change" "line1\nline2\nline3\n" "1,2c\nchanged\n.\nwq\n"
run_oracle_test "Copy (t)" "line1\nline2\nline3\n" "1,2t\$\nwq\n"
run_oracle_test "Move (m)" "line1\nline2\nline3\n" "1m\$\nwq\n"
run_oracle_test "Join (j)" "line1\nline2\nline3\n" "1,2j\nwq\n"
run_oracle_test "Yank and Put (unnamed)" "line1\nline2\nline3\n" "1y\n\$pu\nwq\n"
run_oracle_test "Substitute basic" "hello world\nfoo bar\n" "1s/world/universe/\nwq\n"
run_oracle_test "Substitute global flag" "foo foo foo\nbar\n" "1s/foo/baz/g\nwq\n"
run_oracle_test "Global delete" "foo\nbar\nfoo\nbaz\n" "g/foo/d\nwq\n"
run_oracle_test "Inverse global delete" "foo\nbar\nfoo\nbaz\n" "v/foo/d\nwq\n"
run_oracle_test "Long delete command" "line1\nline2\nline3\n" "2delete\nwq\n"
run_oracle_test "Long copy command" "line1\nline2\nline3\n" "1,2copy\$\nwq\n"
run_oracle_test "Long substitute command" "alpha beta\nbeta gamma\n" "1substitute/beta/BETA/\nwq\n"
run_oracle_test "Long global command" "foo\nbar\nfoo\nbaz\n" "global/foo/delete\nwq\n"
run_oracle_test "Percent range deletes whole file" "line1\nline2\nline3\n" "%delete\nwq\n"

run_stdout_test "Search address with relative offset" \
    "alpha\nbeta\ngamma\ndelta\n" \
    ":/gamma/-1p\n:q!\n" \
    "beta"

run_stdout_test "Backward search address with offset" \
    "alpha\nbeta\ngamma\ndelta\n" \
    ":?beta?+1p\n:q!\n" \
    "gamma"

run_stdout_test "Empty search reuses last search pattern" \
    "alpha\nbeta\ngamma\ndelta\n" \
    ":/gamma/p\n://p\n:q!\n" \
    "gamma
gamma"

run_stdout_test "Semicolon range uses first address as current" \
    "alpha\nbeta\ngamma\ndelta\n" \
    ":1;+2p\n:q!\n" \
    "alpha
beta
gamma"

run_stdout_test "Classic mark command and mark address" \
    "alpha\nbeta\ngamma\n" \
    ":2ka\n:'ap\n:q!\n" \
    "beta"

run_stdout_test "Long print command" \
    "alpha\nbeta\n" \
    ":1print\n:q!\n" \
    "alpha"

run_stdout_test "Set number enables numbered print" \
    "alpha\nbeta\n" \
    ":set nu\n:1p\n:q!\n" \
    "     1  alpha"

run_stdout_test "Set nonumber disables numbered print" \
    "alpha\nbeta\n" \
    ":set nu\n:set nonu\n:1p\n:q!\n" \
    "alpha"

run_stdout_test "Empty command prints addressed line" \
    "alpha\nbeta\ngamma\n" \
    ":2\n:q!\n" \
    "beta"

run_stdout_test "Percent range prints whole file" \
    "alpha\nbeta\ngamma\n" \
    ":%p\n:q!\n" \
    "alpha
beta
gamma"

run_stdout_with_auxfile_test "Read appends after current line by default" \
    "one\ntwo\n" \
    "inserted-a\ninserted-b\n" \
    ":r __READ_FILE__\n:1,4p\n:q!\n" \
    "one
two
inserted-a
inserted-b"

run_oracle_test "Substitute empty pattern reuses previous regex" \
    "alpha beta\nbeta beta\n" \
    "1s/beta/BETA/\n2s//BETA/g\nwq\n"

run_oracle_test "Repeat substitute with ampersand" \
    "alpha beta\nbeta gamma\n" \
    "1s/beta/BETA/\n2&\nwq\n"

run_stderr_status_test "Quit rejects modified buffer" \
    "one\ntwo\n" \
    ":1d\n:q\n" \
    0 \
    "No write since last change (add ! to override)"

run_stderr_status_test "Set rejects unknown option" \
    "one\ntwo\n" \
    ":set frobnicate\n:q!\n" \
    0 \
    "Unknown option: frobnicate"

run_stdout_test "Set number query reports state" \
    "one\ntwo\n" \
    ":set number?\n:q!\n" \
    "nonumber"

run_stdout_test "Set all prints enabled options" \
    "one\ntwo\n" \
    ":set number\n:set all\n:q!\n" \
    "number"

echo "$TEST_PASS tests run."
exit $((TEST_FAILS > 0 ? 1 : 0))
