#!/bin/sh
# scripts/resolve-trap.sh — resolve a substrate kernel TRAP block to
# source locations.
#
# Usage:
#   ./scripts/resolve-trap.sh                    # reads trap text from stdin
#   ./scripts/resolve-trap.sh trap.txt           # reads from file
#   ./scripts/resolve-trap.sh <<EOF
#   TRAP: pid=33 (Xfbdev) ... eip=0x080C17A2 ...
#   TRAP: user backtrace:
#     #0 ebp=0xBFFFF948 ret=0x080DB370
#     ...
#   EOF
#
# Library map is hard-coded from the substrate ldd output observed
# this session.  If the map ever changes, edit LIBMAP below.

set -eu

# Library load addresses (from `ldd $(which Xfbdev)` on substrate).
# Format: "lo hi /path/to/lib"
LIBMAP="
0x40000000 0x40400000 /opt/substrate/i386-unknown-substrate/lib/ld.so
0x00010000 0x0035c000 /opt/substrate/i386-unknown-substrate/lib/libcrypto.so.3
0x0035c000 0x00362000 /opt/substrate/i386-unknown-substrate/lib/libfontenc.so.1
0x00362000 0x0038e000 /opt/substrate/i386-unknown-substrate/lib/libXfont.so.1
0x0038e000 0x00391000 /opt/substrate/i386-unknown-substrate/lib/libXau.so.6
0x00391000 0x00393000 /opt/substrate/i386-unknown-substrate/lib/libxshmfence.so.1
0x00393000 0x00398000 /opt/substrate/i386-unknown-substrate/lib/libXdmcp.so.6
0x00398000 0x0039c000 /opt/substrate/i386-unknown-substrate/lib/libdl.so.0
0x0039c000 0x003b7000 /opt/substrate/i386-unknown-substrate/lib/libm.so.0
0x003b7000 0x003ed000 /home/segin/substrate/lib/c/libc.so.0
0x003ed000 0x00403000 /opt/substrate/i386-unknown-substrate/lib/libz.so.1
0x00403000 0x0042e000 /opt/substrate/i386-unknown-substrate/lib/libgcc_s.so.1
0x0042e000 0x00440000 /opt/substrate/i386-unknown-substrate/lib/libsys.so.0
0x08048000 0xffffffff /home/segin/substrate/contrib/xorg-server/build/build-stage-substrate/hw/kdrive/fbdev/Xfbdev
"

ADDR2LINE="$(command -v i386-unknown-substrate-addr2line 2>/dev/null \
    || ls /opt/substrate/bin/i386-unknown-substrate-addr2line 2>/dev/null \
    || command -v addr2line)"
if [ -z "${ADDR2LINE:-}" ]; then
    echo "resolve-trap.sh: no addr2line in PATH" >&2
    exit 1
fi

# Read input — stdin or first arg's file.
if [ $# -ge 1 ] && [ -f "$1" ]; then
    INPUT=$(cat "$1")
else
    INPUT=$(cat)
fi

# Resolve a single hex address.  Find the matching library by range,
# subtract the base to get the offset, run addr2line.
resolve_addr() {
    addr=$1
    addr_dec=$(printf '%d' "$addr")
    # Walk the libmap.  Subshell with heredoc keeps it simple.
    echo "$LIBMAP" | while read -r lo hi path rest; do
        [ -z "$lo" ] && continue
        lo_dec=$(printf '%d' "$lo")
        hi_dec=$(printf '%d' "$hi")
        if [ "$addr_dec" -ge "$lo_dec" ] && [ "$addr_dec" -lt "$hi_dec" ]; then
            off=$(printf '0x%x' $(( addr_dec - lo_dec )))
            lib=$(basename "$path")
            if [ ! -f "$path" ]; then
                printf '  %s @%s: <library file missing: %s>\n' "$lib" "$off" "$path"
                return 0
            fi
            # Run addr2line.  For the main executable use the address
            # directly (PIE binaries: subtract; ET_EXEC: use absolute).
            if echo "$path" | grep -q 'Xfbdev$'; then
                # ET_EXEC — addr2line takes absolute
                line=$("$ADDR2LINE" -e "$path" -f -p -i "$addr" 2>/dev/null | head -1)
            else
                # Shared library — addr2line takes offset
                line=$("$ADDR2LINE" -e "$path" -f -p -i "$off" 2>/dev/null | head -1)
            fi
            printf '  %s@%s -> %s\n' "$lib" "$off" "${line:-??}"
            return 0
        fi
    done
}

echo "==================================================================="
echo "TRAP RESOLVER"
echo "==================================================================="

# Extract EIP from "eip=0xXXXXXXXX"
EIP=$(echo "$INPUT" | sed -n 's/.*eip=\(0x[0-9A-Fa-f]\+\).*/\1/p' | head -1)
ADDR=$(echo "$INPUT" | sed -n 's/.* addr \(0x[0-9A-Fa-f]\+\) .*/\1/p' | head -1)

if [ -n "$EIP" ]; then
    echo "EIP (fault address): $EIP"
    [ -n "$ADDR" ] && echo "CR2 (mem accessed):  $ADDR"
    echo "--- crash site ---"
    resolve_addr "$EIP"
    echo ""
fi

# Extract every ret=0xXXXXXXXX
RETS=$(echo "$INPUT" | sed -n 's/.* ret=\(0x[0-9A-Fa-f]\+\).*/\1/p')
if [ -n "$RETS" ]; then
    echo "--- backtrace (each frame's return address) ---"
    i=0
    echo "$RETS" | while read ret; do
        printf '#%d %s\n' "$i" "$ret"
        resolve_addr "$ret"
        i=$((i + 1))
    done
fi
