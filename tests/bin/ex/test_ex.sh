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

run_stdout_with_cliargs_test() {
    local name="$1"
    local init_text="$2"
    local cli_args="$3"
    local script="$4"
    local expected="$5"
    local file
    local output

    file=$(mktemp)
    printf "%b" "$init_text" >"$file"
    # shellcheck disable=SC2086
    output=$(printf "%b" "$script" | "$EX_BIN" -s $cli_args "$file" 2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -f "$file"
        return
    fi

    pass "$name"
    rm -f "$file"
}

run_file_with_cliargs_test() {
    local name="$1"
    local init_text="$2"
    local cli_args="$3"
    local script="$4"
    local expected="$5"
    local file

    file=$(mktemp)
    printf "%b" "$init_text" >"$file"
    # shellcheck disable=SC2086
    printf "%b" "$script" | "$EX_BIN" -s $cli_args "$file" >/dev/null 2>&1 || true

    if ! diff -u <(printf "%b" "$expected") "$file" >/dev/null; then
        fail "$name"
        diff -u <(printf "%b" "$expected") "$file" || true
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

run_stdout_with_fileargs_test() {
    local name="$1"
    local init_text1="$2"
    local init_text2="$3"
    local script="$4"
    local expected_template="$5"
    local file1
    local file2
    local resolved_script
    local expected
    local output

    file1=$(mktemp)
    file2=$(mktemp)
    printf "%b" "$init_text1" >"$file1"
    printf "%b" "$init_text2" >"$file2"
    resolved_script=${script//__FILE1__/$file1}
    resolved_script=${resolved_script//__FILE2__/$file2}
    expected=${expected_template//__FILE1__/$file1}
    expected=${expected//__FILE2__/$file2}
    output=$(printf "%b" "$resolved_script" | "$EX_BIN" -s "$file1" "$file2" 2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -f "$file1" "$file2"
        return
    fi

    pass "$name"
    rm -f "$file1" "$file2"
}

run_stderr_status_with_fileargs_test() {
    local name="$1"
    local init_text1="$2"
    local init_text2="$3"
    local script="$4"
    local expected_status="$5"
    local expected_stderr="$6"
    local file1
    local file2
    local resolved_script
    local stdout_file
    local stderr_file
    local status
    local stderr_text

    file1=$(mktemp)
    file2=$(mktemp)
    stdout_file=$(mktemp)
    stderr_file=$(mktemp)
    printf "%b" "$init_text1" >"$file1"
    printf "%b" "$init_text2" >"$file2"
    resolved_script=${script//__FILE1__/$file1}
    resolved_script=${resolved_script//__FILE2__/$file2}

    set +e
    printf "%b" "$resolved_script" | "$EX_BIN" -s "$file1" "$file2" >"$stdout_file" 2>"$stderr_file"
    status=$?
    set -e

    stderr_text=$(cat "$stderr_file")
    if [ "$status" -ne "$expected_status" ] || [ "$stderr_text" != "$expected_stderr" ]; then
        fail "$name"
        printf 'expected status=%s stderr=%s\nactual status=%s stderr=%s\n' \
            "$expected_status" "$expected_stderr" "$status" "$stderr_text"
        rm -f "$file1" "$file2" "$stdout_file" "$stderr_file"
        return
    fi

    pass "$name"
    rm -f "$file1" "$file2" "$stdout_file" "$stderr_file"
}

run_stdout_with_cliargs_and_fileargs_test() {
    local name="$1"
    local init_text1="$2"
    local init_text2="$3"
    local cli_args="$4"
    local script="$5"
    local expected="$6"
    local file1
    local file2
    local output

    file1=$(mktemp)
    file2=$(mktemp)
    printf "%b" "$init_text1" >"$file1"
    printf "%b" "$init_text2" >"$file2"
    # shellcheck disable=SC2086
    output=$(printf "%b" "$script" | "$EX_BIN" -s $cli_args "$file1" "$file2" 2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -f "$file1" "$file2"
        return
    fi

    pass "$name"
    rm -f "$file1" "$file2"
}

run_tag_test() {
    local name="$1"
    local file_text="$2"
    local tags_text="$3"
    local script="$4"
    local expected_template="$5"
    local tmpdir
    local file
    local expected
    local output

    tmpdir=$(mktemp -d)
    file="$tmpdir/sample.txt"
    printf "%b" "$file_text" >"$file"
    printf "%b" "$tags_text" >"$tmpdir/tags"
    expected=${expected_template//__FILE__/$file}

    output=$(cd "$tmpdir" && printf "%b" "$script" | "$EX_BIN" -s "$file" 2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -rf "$tmpdir"
        return
    fi

    pass "$name"
    rm -rf "$tmpdir"
}

run_tag_multifile_test() {
    local name="$1"
    local file1_text="$2"
    local file2_text="$3"
    local tags_text="$4"
    local script="$5"
    local expected="$6"
    local tmpdir
    local file1
    local file2
    local output

    tmpdir=$(mktemp -d)
    file1="$tmpdir/file1.txt"
    file2="$tmpdir/file2.txt"
    printf "%b" "$file1_text" >"$file1"
    printf "%b" "$file2_text" >"$file2"
    printf "%b" "$tags_text" >"$tmpdir/tags"

    output=$(cd "$tmpdir" && printf "%b" "$script" | "$EX_BIN" -s "$file1" 2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -rf "$tmpdir"
        return
    fi

    pass "$name"
    rm -rf "$tmpdir"
}

run_recover_test() {
    local name="$1"
    local init_text="$2"
    local script="$3"
    local recover_script="$4"
    local expected="$5"
    local file
    local output

    file=$(mktemp)
    printf "%b" "$init_text" >"$file"
    printf "%b" "$script" | "$EX_BIN" -s "$file" >/dev/null 2>&1 || true
    output=$(printf "%b" "$recover_script" | "$EX_BIN" -s -r "$file" 2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -f "$file" "$file.recover"
        return
    fi

    pass "$name"
    rm -f "$file" "$file.recover"
}

run_write_file_test() {
    local name="$1"
    local init_text="$2"
    local script_template="$3"
    local expected_output="$4"
    local file
    local out_file
    local script
    local actual_output

    file=$(mktemp)
    out_file=$(mktemp)
    printf "%b" "$init_text" >"$file"
    : >"$out_file"
    script=${script_template//__OUT_FILE__/$out_file}
    printf "%b" "$script" | "$EX_BIN" -s "$file" >/dev/null 2>&1 || true
    actual_output=$(cat "$out_file")

    if [ "$actual_output" != "$expected_output" ]; then
        fail "$name"
        printf 'expected file output:\n%s\nactual file output:\n%s\n' \
            "$expected_output" "$actual_output"
        rm -f "$file" "$out_file"
        return
    fi

    pass "$name"
    rm -f "$file" "$out_file"
}

run_startup_test() {
    local name="$1"
    local init_text="$2"
    local exrc_text="$3"
    local exrc_mode="$4"
    local script="$5"
    local expected="$6"
    local tmpdir
    local file
    local script_file
    local output

    tmpdir=$(mktemp -d)
    file="$tmpdir/sample.txt"
    script_file="$tmpdir/script.ex"
    printf "%b" "$init_text" >"$file"
    printf "%b" "$exrc_text" >"$tmpdir/.exrc"
    printf "%b" "$script" >"$script_file"
    chmod "$exrc_mode" "$tmpdir/.exrc"
    output=$(env HOME="$tmpdir" "$EX_BIN" -s "$file" <"$script_file" 2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -rf "$tmpdir"
        return
    fi

    pass "$name"
    rm -rf "$tmpdir"
}

run_startup_precedence_test() {
    local name="$1"
    local init_text="$2"
    local home_exrc_text="$3"
    local local_exrc_text="$4"
    local cli_args="$5"
    local exinit_text="$6"
    local script="$7"
    local expected="$8"
    local tmpdir
    local home_dir
    local work_dir
    local file
    local script_file
    local output

    tmpdir=$(mktemp -d)
    home_dir="$tmpdir/home"
    work_dir="$tmpdir/work"
    file="$work_dir/sample.txt"
    script_file="$tmpdir/script.ex"

    mkdir -p "$home_dir" "$work_dir"
    printf "%b" "$init_text" >"$file"
    printf "%b" "$script" >"$script_file"

    if [ -n "$home_exrc_text" ]; then
        printf "%b" "$home_exrc_text" >"$home_dir/.exrc"
        chmod 600 "$home_dir/.exrc"
    fi
    if [ -n "$local_exrc_text" ]; then
        printf "%b" "$local_exrc_text" >"$work_dir/.exrc"
        chmod 600 "$work_dir/.exrc"
    fi

    # shellcheck disable=SC2086
    output=$(cd "$work_dir" && env HOME="$home_dir" EXINIT="$exinit_text" \
        "$EX_BIN" -s $cli_args "$file" <"$script_file" 2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -rf "$tmpdir"
        return
    fi

    pass "$name"
    rm -rf "$tmpdir"
}

run_startup_directory_policy_test() {
    local name="$1"
    local init_text="$2"
    local home_exrc_text="$3"
    local home_dir_mode="$4"
    local local_exrc_text="$5"
    local work_dir_mode="$6"
    local script="$7"
    local expected="$8"
    local tmpdir
    local home_dir
    local work_dir
    local file
    local script_file
    local output

    tmpdir=$(mktemp -d)
    home_dir="$tmpdir/home"
    work_dir="$tmpdir/work"
    file="$work_dir/sample.txt"
    script_file="$tmpdir/script.ex"

    mkdir -p "$home_dir" "$work_dir"
    chmod "$home_dir_mode" "$home_dir"
    chmod "$work_dir_mode" "$work_dir"
    printf "%b" "$init_text" >"$file"
    printf "%b" "$script" >"$script_file"

    if [ -n "$home_exrc_text" ]; then
        printf "%b" "$home_exrc_text" >"$home_dir/.exrc"
        chmod 600 "$home_dir/.exrc"
    fi
    if [ -n "$local_exrc_text" ]; then
        printf "%b" "$local_exrc_text" >"$work_dir/.exrc"
        chmod 600 "$work_dir/.exrc"
    fi

    output=$(cd "$work_dir" && env HOME="$home_dir" "$EX_BIN" -s "$file" <"$script_file" \
        2>/dev/null || true)

    if [ "$output" != "$expected" ]; then
        fail "$name"
        printf 'expected stdout:\n%s\nactual stdout:\n%s\n' "$expected" "$output"
        rm -rf "$tmpdir"
        return
    fi

    pass "$name"
    rm -rf "$tmpdir"
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

run_nofile_stderr_status_test() {
    local name="$1"
    local script="$2"
    local expected_status="$3"
    local expected_stderr="$4"
    local stdout_file
    local stderr_file
    local rc
    local stderr_text

    stdout_file=$(mktemp)
    stderr_file=$(mktemp)

    set +e
    printf "%b" "$script" | "$EX_BIN" -s >"$stdout_file" 2>"$stderr_file"
    rc=$?
    set -e

    stderr_text=$(cat "$stderr_file")
    if [ "$rc" -ne "$expected_status" ] || [ "$stderr_text" != "$expected_stderr" ]; then
        fail "$name"
        printf 'expected status=%s stderr=%s\nactual status=%s stderr=%s\n' \
            "$expected_status" "$expected_stderr" "$rc" "$stderr_text"
        rm -f "$stdout_file" "$stderr_file"
        return
    fi

    pass "$name"
    rm -f "$stdout_file" "$stderr_file"
}

run_stderr_status_with_outfile_test() {
    local name="$1"
    local init_text="$2"
    local script_template="$3"
    local expected_status="$4"
    local expected_stderr="$5"
    local file
    local out_file
    local stdout_file
    local stderr_file
    local script
    local status
    local stderr_text

    file=$(mktemp)
    out_file=$(mktemp)
    stdout_file=$(mktemp)
    stderr_file=$(mktemp)
    printf "%b" "$init_text" >"$file"
    : >"$out_file"
    script=${script_template//__OUT_FILE__/$out_file}

    set +e
    printf "%b" "$script" | "$EX_BIN" -s "$file" >"$stdout_file" 2>"$stderr_file"
    status=$?
    set -e

    stderr_text=$(cat "$stderr_file")
    if [ "$status" -ne "$expected_status" ] || [ "$stderr_text" != "$expected_stderr" ]; then
        fail "$name"
        printf 'expected status=%s stderr=%s\nactual status=%s stderr=%s\n' \
            "$expected_status" "$expected_stderr" "$status" "$stderr_text"
        rm -f "$file" "$out_file" "$stdout_file" "$stderr_file"
        return
    fi

    pass "$name"
    rm -f "$file" "$out_file" "$stdout_file" "$stderr_file"
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

run_stdout_test "Plain print starts at top after file load" \
    "alpha\nbeta\ngamma\n" \
    ":p\n:q!\n" \
    "alpha"

run_stdout_test "Set number enables numbered print" \
    "alpha\nbeta\n" \
    ":set nu\n:1p\n:q!\n" \
    "      1 alpha"

run_stdout_test "Set nonumber disables numbered print" \
    "alpha\nbeta\n" \
    ":set nu\n:set nonu\n:1p\n:q!\n" \
    "alpha"

run_stdout_test "Set list enables list-style print" \
    "a\tb\n" \
    ":set list\n:1p\n:q!\n" \
    "a^Ib$"

run_stdout_test "Set nolist disables list-style print" \
    "a\tb\n" \
    ":set list\n:set nolist\n:1p\n:q!\n" \
    "a	b"

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

run_stdout_test "Equals prints last line number and leaves current unchanged" \
    "alpha\nbeta\ngamma\n" \
    ":=\n:p\n:q!\n" \
    "3
alpha"

run_stdout_test "Addressed equals prints addressed line number and leaves current unchanged" \
    "alpha\nbeta\ngamma\n" \
    ":1,2=\n:p\n:q!\n" \
    "2
alpha"

run_stdout_test "Equals prints zero for an empty buffer" \
    "" \
    ":=\n:q!\n" \
    "0"

run_stdout_test "Number command uses vim-style field width and updates current line" \
    "alpha\nbeta\ngamma\n" \
    ":1,2#\n:p\n:q!\n" \
    "      1 alpha
      2 beta
beta"

run_stdout_test "List command updates current line to the last listed line" \
    "a\tb\nc\n" \
    ":1,2l\n:p\n:q!\n" \
    "a^Ib$
c$
c"

run_write_file_test "Write range writes only addressed lines" \
    "alpha\nbeta\ngamma\n" \
    ":1,2w __OUT_FILE__\n:q!\n" \
    "alpha
beta"

run_write_file_test "Write append appends only addressed lines" \
    "alpha\nbeta\ngamma\n" \
    ":2,3w >> __OUT_FILE__\n:q!\n" \
    "beta
gamma"

run_oracle_test "Copy defaults to current line" \
    "line1\nline2\nline3\n" \
    "2\n:t$\nwq\n"

run_oracle_test "Move defaults to current line" \
    "line1\nline2\nline3\n" \
    "2\n:m$\nwq\n"

run_stdout_with_auxfile_test "Read appends after current line by default" \
    "one\ntwo\n" \
    "inserted-a\ninserted-b\n" \
    ":r __READ_FILE__\n:1,4p\n:q!\n" \
    "one
inserted-a
inserted-b
two"

run_stdout_with_fileargs_test "Args prints active argument list" \
    "one\n" \
    "two\n" \
    ":args\n:q!\n" \
    "[__FILE1__] __FILE2__ "

run_stdout_with_fileargs_test "Next advances to next argument file" \
    "one\n" \
    "two\n" \
    ":next\n:1p\n:q!\n" \
    "two"

run_stdout_with_fileargs_test "Prev returns to previous argument file" \
    "one\n" \
    "two\n" \
    ":next\n:prev\n:1p\n:q!\n" \
    "one"

run_stdout_with_fileargs_test "Next with replacement args resets argument list" \
    "one\n" \
    "two\n" \
    ":next __FILE2__ __FILE1__\n:args\n:1p\n:q!\n" \
    "[__FILE2__] __FILE1__ 
two"

run_stdout_with_fileargs_test "Rewind returns to first argument file" \
    "one\n" \
    "two\n" \
    ":next\n:rewind\n:1p\n:q!\n" \
    "one"

run_stderr_status_with_fileargs_test "Next reports end of argument list" \
    "one\n" \
    "two\n" \
    ":next\n:next\n:q!\n" \
    0 \
    "No more files"

run_stderr_status_with_fileargs_test "Prev reports start of argument list" \
    "one\n" \
    "two\n" \
    ":prev\n:q!\n" \
    0 \
    "No previous files"

run_stderr_status_with_fileargs_test "Next rejects modified buffer" \
    "one\n" \
    "two\n" \
    ":1change\nchanged\n.\n:next\n:q!\n" \
    0 \
    "No write since last change (add ! to override)"

run_stdout_with_fileargs_test "Forced next discards modified buffer and advances" \
    "one\n" \
    "two\n" \
    ":1change\nchanged\n.\n:next!\n:1p\n:q!\n" \
    "two"

run_stdout_with_cliargs_and_fileargs_test "Plus next startup advances argument list before batch commands" \
    "one\n" \
    "two\n" \
    "+next" \
    ":1p\n:q!\n" \
    "two"

run_stdout_with_fileargs_test "Edit hash expands alternate filename" \
    "one\n" \
    "two\n" \
    ":next\n:e #\n:1p\n:q!\n" \
    "one"

run_stdout_with_fileargs_test "Read hash expands alternate filename" \
    "one\n" \
    "two\n" \
    ":next\n:r #\n:1,2p\n:q!\n" \
    "two
one"

run_stdout_test "Read percent expands current filename" \
    "one\ntwo\n" \
    ":r %\n:1,4p\n:q!\n" \
    "one
one
two
two"

run_stdout_with_fileargs_test "File percent preserves alternate filename" \
    "one\n" \
    "two\n" \
    ":next\n:file %\n:file #\n:q!\n" \
    "\"__FILE2__\" 1 lines
\"__FILE1__\" 1 lines"

run_stdout_with_fileargs_test "Edit percent preserves alternate filename" \
    "one\n" \
    "two\n" \
    ":next\n:e %\n:e #\n:1p\n:q!\n" \
    "one"

run_stdout_with_fileargs_test "Edit hash preserves bounceable alternate filename" \
    "one\n" \
    "two\n" \
    ":next\n:e #\n:e #\n:1p\n:q!\n" \
    "two"

run_recover_test "Preserve and -r recover modified buffer" \
    "one\ntwo\n" \
    ":1change\nRECOVERED\n.\n:preserve\n:q!\n" \
    ":1,2p\n:q!\n" \
    "RECOVERED
two"

run_tag_test "Tag jump and pop restore prior location" \
    "alpha\nbeta\ngamma\n" \
    "beta\tsample.txt\t2\n" \
    ":1p\n:tag beta\n:p\n:pop\n:1p\n:q!\n" \
    "alpha
beta
beta
alpha"

run_tag_multifile_test "Cross-file tag jump and pop restore source file" \
    "src-one\nsrc-two\n" \
    "dst-one\ndst-two\n" \
    "dsttag\tfile2.txt\t2\n" \
    ":1p\n:tag dsttag\n:p\n:pop\n:1p\n:q!\n" \
    "src-one
dst-two
dst-two
src-one"

run_stdout_test "Tags reports empty stack" \
    "alpha\nbeta\n" \
    ":tags\n:q!\n" \
    "Tag stack empty"

run_tag_test "Tags reports saved and current tag locations" \
    "alpha\nbeta\ngamma\n" \
    "beta\tsample.txt\t2\n" \
    ":tag beta\n:tags\n:q!\n" \
    "beta
1 __FILE__:1
> sample.txt:2"

run_oracle_test "Substitute empty pattern reuses previous regex" \
    "alpha beta\nbeta beta\n" \
    "1s/beta/BETA/\n2s//BETA/g\nwq\n"

run_oracle_test "Repeat substitute with ampersand" \
    "alpha beta\nbeta gamma\n" \
    "1s/beta/BETA/\n2&\nwq\n"

run_startup_test "Safe .exrc is loaded" \
    "alpha\nbeta\n" \
    "set number\n" \
    600 \
    ":1p\n:q!\n" \
    "      1 alpha"

run_startup_test "Group-writable .exrc is ignored" \
    "alpha\nbeta\n" \
    "set number\n" \
    662 \
    ":1p\n:q!\n" \
    "alpha"

run_startup_precedence_test "Home .exrc loads before local .exrc" \
    "alpha\nbeta\n" \
    "set number\n" \
    "set nonumber\nset list\n" \
    "" \
    "" \
    ":set number?\n:set list?\n:q!\n" \
    "nonumber
list"

run_startup_precedence_test "Secure mode skips home and local .exrc" \
    "alpha\nbeta\n" \
    "set number\n" \
    "set list\n" \
    "-S" \
    "" \
    ":set number?\n:set list?\n:q!\n" \
    "nonumber
nolist"

run_startup_precedence_test "EXINIT overrides exrc loading" \
    "alpha\nbeta\n" \
    "set number\n" \
    "set list\n" \
    "" \
    "set readonly" \
    ":set number?\n:set list?\n:set readonly?\n:q!\n" \
    "nonumber
nolist
readonly"

run_startup_directory_policy_test "Unsafe local directory .exrc is ignored" \
    "alpha\nbeta\n" \
    "set number\n" \
    700 \
    "set nonumber\nset list\n" \
    777 \
    ":set number?\n:set list?\n:q!\n" \
    "number
nolist"

run_startup_directory_policy_test "Unsafe home directory .exrc is ignored" \
    "alpha\nbeta\n" \
    "set number\n" \
    777 \
    "" \
    700 \
    ":set number?\n:q!\n" \
    "nonumber"

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

run_stderr_status_test "Print rejects empty buffer" \
    "" \
    ":p\n:q!\n" \
    0 \
    "No current line"

run_stderr_status_test "Number rejects empty buffer" \
    "" \
    ":#\n:q!\n" \
    0 \
    "No current line"

run_stderr_status_test "List rejects empty buffer" \
    "" \
    ":l\n:q!\n" \
    0 \
    "No current line"

run_stderr_status_test "Empty command rejects empty buffer" \
    "" \
    ":\n:q!\n" \
    0 \
    "No current line"

run_nofile_stderr_status_test "File percent rejects missing current filename" \
    ":file %\n:q!\n" \
    0 \
    "No current filename"

run_nofile_stderr_status_test "File hash rejects missing alternate filename" \
    ":file #\n:q!\n" \
    0 \
    "No alternate filename"

run_nofile_stderr_status_test "Recover percent reports missing current filename once" \
    ":recover %\n:q!\n" \
    0 \
    "No current filename"

run_nofile_stderr_status_test "Recover hash reports missing alternate filename once" \
    ":recover #\n:q!\n" \
    0 \
    "No alternate filename"

run_stderr_status_test "Copy requires destination" \
    "one\ntwo\n" \
    ":1copy\n:q!\n" \
    0 \
    "Destination required"

run_stderr_status_with_outfile_test "Write to another file keeps buffer modified" \
    "one\ntwo\n" \
    ":1change\nchanged\n.\n:w __OUT_FILE__\n:q\n" \
    0 \
    "No write since last change (add ! to override)"

run_stderr_status_test "Move rejects destination inside range" \
    "one\ntwo\nthree\n" \
    ":1,2move1\n:q!\n" \
    0 \
    "Destination not outside move range"

run_stdout_test "Set number query reports state" \
    "one\ntwo\n" \
    ":set number?\n:q!\n" \
    "nonumber"

run_stdout_test "Set list query reports state" \
    "one\ntwo\n" \
    ":set list?\n:q!\n" \
    "nolist"

run_stdout_with_cliargs_test "Readonly startup option reports state" \
    "one\ntwo\n" \
    "-R" \
    ":set readonly?\n:q!\n" \
    "readonly"

run_stdout_with_cliargs_test "Startup -c command runs before batch commands" \
    "alpha\nbeta\ngamma\n" \
    "-c2" \
    ":p\n:q!\n" \
    "beta
beta"

run_stdout_with_cliargs_test "Repeated -c commands preserve order" \
    "alpha\nbeta\ngamma\n" \
    "-c1d -c1p" \
    ":q!\n" \
    "beta"

run_stdout_with_cliargs_test "Plus line command runs before batch commands" \
    "alpha\nbeta\ngamma\n" \
    "+2" \
    ":p\n:q!\n" \
    "beta
beta"

run_stdout_with_cliargs_test "Plus search command runs before batch commands" \
    "alpha\nbeta\ngamma\n" \
    "+/beta" \
    ":p\n:q!\n" \
    "beta
beta"

run_stdout_test "Set tabstop query reports value" \
    "one\ntwo\n" \
    ":set tabstop?\n:q!\n" \
    "tabstop=8"

run_stdout_test "Set wrapscan query reports state" \
    "one\ntwo\n" \
    ":set wrapscan?\n:q!\n" \
    "wrapscan"

run_oracle_test "Substitute replacement pipe does not split command line" \
    "foo\n" \
    ":%s/o/bar|baz/\n:wq!\n"

run_oracle_test "Global nested substitute pipe does not split command line" \
    "foo\nbar\n" \
    ":g/foo/s/o/|/\n:wq!\n"

run_oracle_test "Global nested command still allows later separator" \
    "foo\nbar\n" \
    ":g/foo/s/o/|/|d\n:wq!\n"

run_oracle_test "Read shell pipeline does not split command line" \
    "one\n" \
    ":r !printf 'two\\n' | cat\n:wq!\n"

run_stdout_test "Set trailing comment is ignored" \
    "foo\n" \
    ":set number\"tail\n:1p\n:q!\n" \
    "      1 foo"

run_stdout_test "Address trailing comment preserves empty-command print" \
    "foo\nbar\n" \
    ":1 \"tail\n:q!\n" \
    "foo"

run_oracle_test "Substitute quote in replacement is not a comment" \
    "foo\n" \
    ":%s/o/\"/\n:wq!\n"

run_oracle_test "Global nested substitute quote is not a comment" \
    "foo\nbar\n" \
    ":g/foo/s/o/\"/\n:wq!\n"

run_stdout_test "Set nowrapscan disables wrapscan" \
    "one\ntwo\n" \
    ":set nowrapscan\n:set wrapscan?\n:q!\n" \
    "nowrapscan"

run_stdout_test "Set tabstop assignment updates value" \
    "one\ntwo\n" \
    ":set ts=4\n:set ts?\n:q!\n" \
    "tabstop=4"

run_stdout_test "Set all prints enabled options" \
    "one\ntwo\n" \
    ":set number list readonly\n:set all\n:q!\n" \
    "number
list
readonly
wrapscan
tabstop=8"

run_stdout_test "Set noreadonly disables readonly option" \
    "one\ntwo\n" \
    ":set readonly\n:set noreadonly\n:set readonly?\n:q!\n" \
    "noreadonly"

run_file_with_cliargs_test "Readonly startup blocks plain write" \
    "alpha\n" \
    "-R" \
    "1change\nblocked\n.\nwq\n" \
    "alpha\n"

run_file_with_cliargs_test "Readonly startup allows forced write" \
    "alpha\n" \
    "-R" \
    "1change\nforced\n.\nw!\nq!\n" \
    "forced\n"

echo "$TEST_PASS tests run."
exit $((TEST_FAILS > 0 ? 1 : 0))
