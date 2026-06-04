#!/bin/sh
# tests/bin/arch_test.sh
# Host-side behavioral test for bin/arch.  arch is `uname -m`, so we build it
# for the host and check it agrees with the host's `uname -m`, plus the
# --help / --version / bad-argument handling.
set -e

gcc -Wall -Werror -o arch_host bin/arch/arch.c

fail() { echo "FAIL: $1"; rm -f arch_host; exit 1; }

# 1. arch with no args == uname -m
got=$(./arch_host)
want=$(uname -m)
[ "$got" = "$want" ] || fail "arch -> '$got', expected '$want' (uname -m)"

# 2. exactly one line of output
lines=$(./arch_host | wc -l)
[ "$lines" -eq 1 ] || fail "expected 1 line of output, got $lines"

# 3. --version succeeds and prints something
./arch_host --version >/dev/null || fail "--version exited non-zero"
[ -n "$(./arch_host --version)" ] || fail "--version printed nothing"

# 4. --help succeeds
./arch_host --help >/dev/null || fail "--help exited non-zero"

# 5. unrecognized argument is an error (non-zero exit)
if ./arch_host -x >/dev/null 2>&1; then
	fail "unrecognized argument should exit non-zero"
fi

# 6. a bare operand is also rejected (arch takes none)
if ./arch_host foo >/dev/null 2>&1; then
	fail "operand should be rejected"
fi

echo "PASS: arch ($got)"
rm -f arch_host
