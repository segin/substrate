#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

probe_std_flag() {
    local cc="$1"
    local mode="$2"
    local src="/tmp/cc_postc99_probe_${cc}_${mode}.c"

    echo 'int main(void){return 0;}' > "$src"
    if "$cc" -std="$mode" -x c "$src" -o /tmp/cc_postc99_probe_bin >/dev/null 2>/dev/null; then
        echo "-std=$mode"
        return 0
    fi
    if [ "$mode" = "c23" ] && "$cc" -std=c2x -x c "$src" -o /tmp/cc_postc99_probe_bin >/dev/null 2>/dev/null; then
        echo "-std=c2x"
        return 0
    fi
    echo ""
}

run_one_compiler() {
    local host_cc="$1"
    local mode="$2"
    local src="$3"
    local tag="$4"
    local stdflag
    local ours_rc host_rc

    stdflag=$(probe_std_flag "$host_cc" "$mode")
    if [ -z "$stdflag" ]; then
        return 0
    fi

    "$CC_BIN" -std="$mode" "$src" -o "/tmp/cc_postc99_ours_${tag}"
    "$host_cc" "$stdflag" "$src" -o "/tmp/cc_postc99_host_${tag}"

    "/tmp/cc_postc99_ours_${tag}"
    ours_rc=$?
    "/tmp/cc_postc99_host_${tag}"
    host_rc=$?

    test "$ours_rc" -eq "$host_rc"
}

run_compiler_matrix() {
    local host_cc="$1"
    run_one_compiler "$host_cc" c11 native_c11_generic.c "${host_cc}_c11"
    run_one_compiler "$host_cc" c11 native_c11_static_assert.c "${host_cc}_c11_static_assert"
    run_one_compiler "$host_cc" c17 native_c17_register.c "${host_cc}_c17"
    run_one_compiler "$host_cc" c23 native_c23_binary_sep.c "${host_cc}_c23"
    run_one_compiler "$host_cc" c23 native_c23_typeof.c "${host_cc}_c23_typeof"
}

if command -v gcc >/dev/null 2>&1; then
    run_compiler_matrix gcc
fi

if command -v clang >/dev/null 2>&1; then
    run_compiler_matrix clang
fi
