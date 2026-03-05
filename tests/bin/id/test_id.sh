#!/bin/bash
# Tests for bin/id
# Note: explicit exit-code checks for must-fail cases; no set -e reliance there.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"

ID_BIN="$REPO_ROOT/bin/id/id"

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

assert_eq() {
    local got="$1"
    local exp="$2"
    local msg="$3"
    if [[ "$got" != "$exp" ]]; then
        fail "$msg (got='$got' exp='$exp')"
    fi
}

assert_contains() {
    local got="$1"
    local exp="$2"
    local msg="$3"
    if [[ "$got" != *"$exp"* ]]; then
        fail "$msg (got='$got' did not contain '$exp')"
    fi
}

build_id() {
    make -C "$REPO_ROOT/bin/id" NATIVE_BUILD=1 >/dev/null
    if [[ ! -x "$ID_BIN" ]]; then
        fail "Could not build $ID_BIN"
    fi
}

main() {
    build_id

    # Create a temporary environment if needed, or just use current user
    # Note: id without arguments applies to the current user.
    local my_uid="$(id -u)"
    local my_user="$(id -un)"
    local my_gid="$(id -g)"
    local my_group="$(id -gn)"
    local my_groups="$(id -G)"

    # Base tests
    echo "--- ID-REQ-POSIX-001: -u ---"
    got="$("$ID_BIN" -u)"
    assert_eq "$got" "$my_uid" "default -u"

    echo "--- ID-REQ-POSIX-002: -g ---"
    got="$("$ID_BIN" -g)"
    assert_eq "$got" "$my_gid" "default -g"

    echo "--- ID-REQ-POSIX-003: -G ---"
    got="$("$ID_BIN" -G)"
    assert_eq "$got" "$my_groups" "default -G"

    echo "--- ID-REQ-NAME-002: -un and -gn ---"
    got="$("$ID_BIN" -un)"
    assert_eq "$got" "$my_user" "-un user name"
    got="$("$ID_BIN" -gn)"
    assert_eq "$got" "$my_group" "-gn group name"
    got="$("$ID_BIN" -Gn)"
    sys_groups="$(id -Gn)"
    assert_eq "$got" "$sys_groups" "-Gn group names"

    echo "--- ID-REQ-CLI-003: default format ---"
    # POSIX default: uid=... gid=... groups=...
    got="$("$ID_BIN")"
    sys_id="$(id)"
    # Some host id's include SELinux context which we might not. Standard POSIX prefix check:
    # "uid=... gid=... groups=..."
    # The groups list might not match perfectly if host 'id' output includes 'context=' at the end (like GNU does).
    # We strip 'context=' for a fair comparison.
    sys_id="${sys_id% context=*}"
    assert_eq "$got" "$sys_id" "default full id string"

    echo "--- ID-REQ-BSD-002: -P ---"
    got="$("$ID_BIN" -P)"
    sys_p=":" # Passwd format, at least starts/contains name
    assert_contains "$got" "$my_user:x:$my_uid:$my_gid:" "-P output"

    echo "--- ID-REQ-GNU-002: --zero ---"
    got="$("$ID_BIN" -G -z | od -c | tr '\n' ' ')"
    # NUL delimited means it has \0
    assert_contains "$got" "\0" "--zero contains NUL"

    echo "--- ID-REQ-CLI-002: invalid options ---"
    rc=0; "$ID_BIN" -x >/dev/null 2>&1 || rc=$?
    [[ $rc -ne 0 ]] || fail "should fail on invalid option"

    echo "--- ID-REQ-CLI-004: --help/--version ---"
    "$ID_BIN" --version | grep -q "id" || fail "--version missing 'id'"

    echo "All id tests passed."
}

main "$@"
