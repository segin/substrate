#!/bin/sh
#
# test_stty.sh — functional tests for bin/stty.
#
# stty manipulates a terminal, so it is exercised against a real
# pseudo-terminal.  The Substrate target binary is an ELFOSABI
# Substrate object that cannot run on the build host, so this test
# follows the project's host-validation model (see AGENTS.md
# "Host Builds vs Target Builds"): it compiles bin/stty/stty.c
# with the host compiler and runs the resulting binary.
#
# Covers docs/specs/stty.md STTY-TST-001..006.

set -u

SRC=
for c in bin/stty/stty.c ../../../bin/stty/stty.c ../../bin/stty/stty.c \
         ./stty.c; do
	if [ -f "$c" ]; then SRC="$c"; break; fi
done
if [ -z "$SRC" ]; then
	echo "test_stty: cannot locate stty.c" >&2
	exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
	echo "test_stty: SKIP (python3 required for pty harness)"
	exit 0
fi

CC="${CC:-cc}"
BIN="$(mktemp)" || exit 1
trap 'rm -f "$BIN"' EXIT

if ! "$CC" -D_GNU_SOURCE -Wall -Wextra -std=gnu2x -o "$BIN" "$SRC"; then
	echo "test_stty: host compile failed" >&2
	exit 1
fi

python3 - "$BIN" <<'PYEOF'
import os, sys, subprocess

STTY = sys.argv[1]
master, slave = os.openpty()      # one shared pty for the whole run

fails = 0
def check(cond, name):
    global fails
    if cond:
        print("PASS:", name)
    else:
        print("FAIL:", name)
        fails += 1

def run(args):
    r = subprocess.run([STTY] + args, stdin=slave,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return r.returncode, r.stdout.decode()

def gfield(token, key):
    for kv in token.strip().split(":"):
        if kv.startswith(key + "="):
            return kv.split("=", 1)[1]
    return None

# STTY-TST-001 — gfmt1 save/restore is an identity operation.
rc, g1 = run(["-g"])
rc_apply, _ = run([g1.strip()])
rc, g2 = run(["-g"])
check(rc_apply == 0 and g1.strip() == g2.strip(), "STTY-TST-001 gfmt1 round-trip")

# STTY-TST-002 — boolean mode set/clear reaches the termios bit.
ECHO = 0x8
run(["-echo"]);  _, goff = run(["-g"])
run(["echo"]);   _, gon  = run(["-g"])
off = int(gfield(goff, "lflag"), 16) & ECHO
on  = int(gfield(gon,  "lflag"), 16) & ECHO
check(off == 0 and on == ECHO, "STTY-TST-002 mode set/clear")

# STTY-TST-003 — control-character value parsing.
run(["intr", "0x07"]);  _, g = run(["-g"]); hexok = gfield(g, "intr") == "7"
run(["intr", "^A"]);    _, g = run(["-g"]); ctlok = gfield(g, "intr") == "1"
run(["intr", "undef"]); _, g = run(["-g"]); undok = gfield(g, "intr") == "0"
run(["intr", "9"]);     _, g = run(["-g"]); decok = gfield(g, "intr") == "9"
check(hexok and ctlok and undok and decok, "STTY-TST-003 cc value parsing")
run(["intr", "^C"])     # restore

# STTY-TST-004 — unrecognised operand fails with a diagnostic.
rc, out = run(["frobnicate"])
check(rc != 0 and "frobnicate" in out and out.startswith("stty:"),
      "STTY-TST-004 bad operand rejected")

# STTY-TST-005 — info-option exclusivity error paths.
rc_ag, _ = run(["-a", "-g"])
rc_io, _ = run(["-a", "echo"])
check(rc_ag != 0 and rc_io != 0, "STTY-TST-005 info-option exclusivity")

# STTY-TST-006 — query operand output shapes.
_, sz = run(["size"])
_, sp = run(["speed"])
szok = len(sz.split()) == 2 and all(p.isdigit() for p in sz.split())
spok = sp.strip().isdigit()
check(szok and spok, "STTY-TST-006 size/speed query shape")

# Extra — a malformed save token is rejected without applying.
rc, out = run(["gfmt1:bogus"])
check(rc != 0 and out.startswith("stty:"), "extra: malformed gfmt1 rejected")

os.close(master); os.close(slave)
print()
if fails:
    print("test_stty: %d failure(s)" % fails)
    sys.exit(1)
print("test_stty: all tests passed")
PYEOF
