#!/bin/sh
# tests/bin/mv/test_integration.sh — POSIX.1-2024 + GNU + BSD mv
# integration tests.  Pass path to the mv binary as $1.
#
# Each scenario builds its own sandbox, asserts state, and reports.
# Exit non-zero on any failure.
set -u

MV="${1:-./mv_host}"
# Resolve to absolute path before any cd's.
case "$MV" in
    /*) ;;
    *)  MV="$PWD/$MV" ;;
esac
if [ ! -x "$MV" ]; then
    echo "test_integration: $MV not executable" >&2
    exit 2
fi

PASS=0
FAIL=0
T="$(mktemp -d)"
trap 'cd /; rm -rf "$T"' EXIT INT TERM

ok()   { PASS=$((PASS+1)); echo "  ok   $1"; }
nok()  { FAIL=$((FAIL+1)); echo "  FAIL $1"; }
case_dir() {
    local name="$1"; shift
    local d="$T/$name"
    mkdir -p "$d"
    cd "$d"
}

# --- POSIX: basic rename --------------------------------------------------
case_dir basic_rename
echo "A" > a
"$MV" a b
[ ! -e a ] && [ "$(cat b)" = "A" ] && ok basic_rename || nok basic_rename

# --- POSIX: rename when dest exists (default = force) --------------------
case_dir overwrite_default
echo "A" > a
echo "B" > b
"$MV" a b
[ ! -e a ] && [ "$(cat b)" = "A" ] && ok overwrite_default || nok overwrite_default

# --- POSIX: move into existing directory ---------------------------------
case_dir into_dir
echo "A" > a
mkdir d
"$MV" a d/
[ -f d/a ] && [ "$(cat d/a)" = "A" ] && ok into_dir || nok into_dir

# --- POSIX: multi-source into directory ----------------------------------
case_dir multi_into_dir
echo 1 > a; echo 2 > b; echo 3 > c
mkdir d
"$MV" a b c d
[ -f d/a ] && [ -f d/b ] && [ -f d/c ] && ok multi_into_dir || nok multi_into_dir

# --- POSIX: -i no (decline) ----------------------------------------------
case_dir interactive_no
echo "old" > a
echo "new" > b
printf "n\n" | "$MV" -i a b >/dev/null 2>&1
[ -f a ] && [ "$(cat b)" = "new" ] && ok interactive_no || nok interactive_no

# --- POSIX: -i yes -------------------------------------------------------
case_dir interactive_yes
echo "old" > a
echo "new" > b
printf "y\n" | "$MV" -i a b >/dev/null 2>&1
[ ! -e a ] && [ "$(cat b)" = "old" ] && ok interactive_yes || nok interactive_yes

# --- POSIX: -n skip ------------------------------------------------------
case_dir noclobber
echo "X" > a
echo "Y" > b
"$MV" -n a b
[ -f a ] && [ "$(cat b)" = "Y" ] && ok noclobber || nok noclobber

# --- POSIX: -finvn last-wins (final flag = -f) ----------------------------
case_dir last_wins
echo "X" > a
echo "Y" > b
"$MV" -inf a b
[ ! -e a ] && [ "$(cat b)" = "X" ] && ok last_wins || nok last_wins

# --- POSIX: -- end-of-options --------------------------------------------
case_dir end_of_opts
echo "X" > ./-f
"$MV" -- ./-f g 2>/dev/null
[ ! -e ./-f ] && [ -f g ] && ok end_of_opts || nok end_of_opts

# --- GNU: -v (verbose) ---------------------------------------------------
case_dir verbose
echo "X" > a
"$MV" -v a b 2>/dev/null > log
grep -q "renamed 'a' -> 'b'" log && ok verbose || nok verbose

# --- GNU: -b simple backup -----------------------------------------------
case_dir backup_simple
echo "old" > a
echo "new" > b
"$MV" -b a b
[ -f b~ ] && [ "$(cat b~)" = "new" ] && [ "$(cat b)" = "old" ] && ok backup_simple || nok backup_simple

# --- GNU: -S custom suffix ----------------------------------------------
case_dir backup_suffix
echo "old" > a
echo "new" > b
"$MV" -b -S .bak a b
[ -f b.bak ] && [ "$(cat b.bak)" = "new" ] && ok backup_suffix || nok backup_suffix

# --- GNU: --backup=numbered ---------------------------------------------
case_dir backup_numbered
echo "1" > a; echo "old" > b
"$MV" --backup=numbered a b
[ -f b.~1~ ] && [ "$(cat b.~1~)" = "old" ] && ok backup_numbered || nok backup_numbered

# --- GNU: --backup=numbered picks next N --------------------------------
case_dir backup_numbered_next
echo "old" > b
echo "n1" > b.~1~
echo "n2" > b.~2~
echo "new" > a
"$MV" --backup=numbered a b
[ -f b.~3~ ] && [ "$(cat b.~3~)" = "old" ] && ok backup_numbered_next || nok backup_numbered_next

# --- GNU: -T forbids directory-target semantics --------------------------
case_dir T_replaces_dir
mkdir src
echo "X" > src/file
"$MV" -T src dst
[ -f dst/file ] && [ ! -e src ] && ok T_replaces_dir || nok T_replaces_dir

# --- GNU: -t TARGET_DIR sources ... --------------------------------------
case_dir t_target
echo "1" > a; echo "2" > b
mkdir D
"$MV" -t D a b
[ -f D/a ] && [ -f D/b ] && ok t_target || nok t_target

# --- BSD/GNU: same-file detection ----------------------------------------
case_dir same_file
echo "X" > a
"$MV" a a 2>err
grep -q "same file" err && ok same_file || nok same_file

# --- GNU: --update=older skips when src older ---------------------------
case_dir update_older
echo "src" > a
echo "dst" > b
touch -d "2020-01-01" a
touch -d "2030-01-01" b
"$MV" --update=older a b
[ "$(cat b)" = "dst" ] && [ -f a ] && ok update_older || nok update_older

# --- GNU: --strip-trailing-slashes treats SRC/ as SRC --------------------
case_dir strip_slashes
mkdir src
echo "X" > src/file
mkdir dst
"$MV" --strip-trailing-slashes src/ dst/
[ -d dst/src ] && [ -f dst/src/file ] && ok strip_slashes || nok strip_slashes

# --- Symlink: rename, do not dereference ---------------------------------
case_dir symlink_rename
ln -s /no/such/target lnk
"$MV" lnk lnk2
[ -L lnk2 ] && [ "$(readlink lnk2)" = "/no/such/target" ] && ok symlink_rename || nok symlink_rename

# --- Directory rename within filesystem ----------------------------------
case_dir dir_rename
mkdir -p old/sub
echo "X" > old/sub/file
"$MV" old new
[ -f new/sub/file ] && [ ! -e old ] && ok dir_rename || nok dir_rename

# --- Error: missing source ----------------------------------------------
case_dir err_missing_src
"$MV" nonexistent dst 2>err && nok err_missing_src || ok err_missing_src

# --- Error: cannot move into self ---------------------------------------
case_dir err_into_self
mkdir d
"$MV" d d 2>err && nok err_into_self || ok err_into_self

echo
echo "mv integration: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
