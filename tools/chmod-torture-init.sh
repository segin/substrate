#!/bin/sh
# chmod torture test
#
# Runs inside Substrate as /sbin/init.  Hammers both the chmod(2)
# syscall (via the chmod CLI) and the bin/chmod parser/CLI surface.
# Logs PASS/FAIL per case to serial; final tally + exit at the end.
#
# Replace dist/sbin/init with this file then re-image (or debugfs-
# inject as /sbin/init) and boot.

PASS=0
FAIL=0
fail() {
  FAIL=$((FAIL + 1))
  echo "[FAIL] $*"
}
pass() {
  PASS=$((PASS + 1))
}

# get_mode <path> → echoes the 9-char rwx permission string from ls -l.
# No sed (sed doesn't compile under Substrate yet), so use shell
# parameter expansion: drop the first byte (file-type slot) and then
# truncate at the first space.
get_mode() {
  # `ls -l <dir>` prints a `total N` header; `ls -ld` skips it and
  # gives one line for the directory itself.  Use -ld unconditionally
  # so the same path works for files and dirs.
  line=$(/bin/ls -ld "$1" 2>/dev/null)
  [ -z "$line" ] && return
  perm=${line#?}
  echo "${perm%% *}"
}

# expect_octal <path> <expected-3-char-rwx-string>
expect_perm() {
  got=$(get_mode "$1")
  if [ "$got" = "$2" ]; then
    pass
  else
    fail "$1: expected $2, got $got"
  fi
}

# ---------------------------------------------------------------- mkfile
TMP=/tmp/cht
/bin/mkdir -p $TMP
cd $TMP || { echo "cannot cd to $TMP"; exit 1; }

echo "[t] === chmod torture test ==="
echo "[t] sh umask = $(umask 2>/dev/null)"
# Force umask 022 so the test cases match POSIX-with-default-umask semantics.
umask 022
echo "[t] sh umask after set = $(umask 2>/dev/null)"

# ---------------------------------------------------------------- octal
echo "[t] phase 1: octal modes"
/bin/echo data > f
for mode in 000 001 010 100 077 700 444 555 644 666 700 755 777 421 765 543 321; do
  /bin/chmod $mode f && pass || fail "chmod $mode f returned nonzero"
done

# Verify a few specific modes we can decode by hand
/bin/chmod 644 f; expect_perm f "rw-r--r--"
/bin/chmod 755 f; expect_perm f "rwxr-xr-x"
/bin/chmod 700 f; expect_perm f "rwx------"
/bin/chmod 444 f; expect_perm f "r--r--r--"
/bin/chmod 000 f; expect_perm f "---------"
/bin/chmod 777 f; expect_perm f "rwxrwxrwx"

# ---------------------------------------------------------------- symbolic
echo "[t] phase 2: symbolic modes"
/bin/chmod 000 f
/bin/chmod u+r f;       expect_perm f "r--------"
/bin/chmod u+wx f;      expect_perm f "rwx------"
/bin/chmod g+rx f;      expect_perm f "rwxr-x---"
/bin/chmod o=r f;       expect_perm f "rwxr-xr--"
/bin/chmod a+w f;       expect_perm f "rwxrwxrw-"
/bin/chmod a-x f;       expect_perm f "rw-rw-rw-"
/bin/chmod =r f;        expect_perm f "r--r--r--"
/bin/chmod u=rwx,go=r f; expect_perm f "rwxr--r--"
/bin/chmod 644 f; /bin/chmod -w f; expect_perm f "r--r--r--"
/bin/chmod 644 f; /bin/chmod +x f; expect_perm f "rwxr-xr-x"

# ---------------------------------------------------------------- bad input
echo "[t] phase 3: malformed modes (must fail)"
for bad in 8 9 999 abc xyz "u+" "+" "" "@"; do
  /bin/chmod "$bad" f >/dev/null 2>&1
  if [ $? -eq 0 ]; then
    fail "chmod accepted malformed mode '$bad'"
  else
    pass
  fi
done
# But chmod with no args should fail too
/bin/chmod 2>/dev/null; [ $? -ne 0 ] && pass || fail "chmod with no args succeeded"
/bin/chmod 644 nonexistent_file 2>/dev/null
[ $? -ne 0 ] && pass || fail "chmod nonexistent succeeded"

# ---------------------------------------------------------------- recursion
echo "[t] phase 4: recursive (-R)"
/bin/mkdir -p sub/a sub/b sub/c
/bin/echo x > sub/a/x
/bin/echo y > sub/b/y
/bin/echo z > sub/c/z
/bin/chmod -R 755 sub
expect_perm sub      "rwxr-xr-x"
expect_perm sub/a    "rwxr-xr-x"
expect_perm sub/a/x  "rwxr-xr-x"
expect_perm sub/c/z  "rwxr-xr-x"
/bin/chmod -R 600 sub
expect_perm sub      "rw-------"
expect_perm sub/a/x  "rw-------"

# ---------------------------------------------------------------- volume
echo "[t] phase 5: volume (1000 chmods on one file)"
# Iterate decimal 0..777.  Of those, only the ones whose digits are
# all 0..7 are valid octal (8^3 = 512 in 0..777; counting 0..7 +
# 10..77 + 100..777 with octal-only digits).  Expect chmod to accept
# exactly those and reject the rest.
/bin/chmod 644 f
i=0
ok=0
fails=0
expected_ok=0
while [ $i -lt 1000 ]; do
  m=$((i % 778))
  # Digit-validity check: any 8 or 9 means invalid octal.
  case $m in
    *8*|*9*) ;;
    *) expected_ok=$((expected_ok+1)) ;;
  esac
  /bin/chmod $m f >/dev/null 2>&1
  if [ $? -eq 0 ]; then ok=$((ok+1)); else fails=$((fails+1)); fi
  i=$((i+1))
done
echo "[t]   1000-iter loop: ok=$ok fail=$fails expected_ok=$expected_ok"
[ $ok -eq $expected_ok ] && pass || fail "volume loop: ok=$ok != expected_ok=$expected_ok"

# ---------------------------------------------------------------- toggling
echo "[t] phase 6: bit toggle stability"
/bin/chmod 000 f
i=0
while [ $i -lt 100 ]; do
  /bin/chmod +x f
  /bin/chmod -x f
  i=$((i+1))
done
expect_perm f "---------"

# ---------------------------------------------------------------- tally
echo "[t] === chmod torture done ==="
echo "[t] PASS=$PASS FAIL=$FAIL"
[ $FAIL -eq 0 ] && echo "[t] === ALL PASS ===" || echo "[t] === FAILURES: $FAIL ==="
