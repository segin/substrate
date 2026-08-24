#!/bin/sh
#
# xenix - run a SCO Xenix/286 program with an environment MSTOOLS understands.
#
# Xenix binaries run natively under the PERS_SCO_X286 personality, so this is
# not an emulator wrapper -- it exists purely to fix up the environment, and
# TERM is the whole reason.
#
# Two things go wrong without it:
#
#   TERM=linux    substrate's login(1) sets this, and Word's terminal database
#                 (/usr/lib/MSTOOLS/termdesc) has no such entry.  Word exits
#                 immediately with "No termdesc entry for linux".
#
#   TERM=ansi     the entry exists, but it is the LARGEST in that file at
#                 9859 bytes, and Word 3.0 cannot fit it.  Word's near-heap
#                 arena is a hard-coded 31744 bytes (the constant lives at
#                 text 0x5f:0xb820), all of it inside the single 64 KiB
#                 DGROUP a small-data 286 program gets, and with the ansi
#                 description loaded it runs out and prints
#                 "Insufficient memory / MEMORY ERROR!".  This is not the
#                 kernel refusing anything -- traced with every brk refusal
#                 logged, Word never asks for more memory at all.
#
#                 Measured, everything else identical:
#                     vt52   5023  ok      console.sco    8954  ok
#                     vt100  6803  ok      color_console  9793  ok
#                     wyse50 7234  ok      ansi           9859  FAILS
#                 66 bytes decide it.
#
# vt100 is the default here because substrate's console speaks VT100/ANSI
# escape sequences and its description is comfortably inside the budget.
# Override with XENIX_TERM for a program that wants something else.
#
# Usage:  xenix /perso/xenix286s/usr/bin/word [args...]
#         XENIX_TERM=vt52 xenix /perso/xenix286s/bin/vi file

if [ $# -lt 1 ]; then
    echo "usage: xenix <program> [args...]" >&2
    echo "       XENIX_TERM=<term> overrides the default (vt100)" >&2
    exit 64
fi

: "${XENIX_TERM:=vt100}"
TERM=$XENIX_TERM
export TERM

# MSTOOLS programs write scratch files here and do not create it themselves.
# Path is relative to the personality root, so this is the Xenix /usr/tmp.
[ -d /perso/xenix286s/usr/tmp ] || mkdir -p /perso/xenix286s/usr/tmp 2>/dev/null

exec "$@"
