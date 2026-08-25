#!/bin/sh
#
# xenix - run a SCO Xenix/286 program with an environment MSTOOLS understands.
#
# Xenix binaries run natively under the PERS_SCO_X286 personality, so this is
# not an emulator wrapper -- it exists purely to fix up the environment, and
# TERM is the whole reason.
#
# Word 3.0 reads its terminal description from /usr/lib/MSTOOLS/termdesc into
# a near-heap arena of exactly 31744 bytes -- a hard-coded constant at text
# 0x5f:0xb820 -- inside the single 64 KiB DGROUP a small-data 286 program
# gets (x_renv 0xc847: XE_LTEXT set, XE_LDATA clear).  It asks for that much
# and gets it; the kernel is never asked for more, so no amount of generosity
# on our side changes anything.  Whether Word survives therefore comes down
# to how big the description is.  Measured on target:
#
#     vt52    5023  ok      console        8881  ok
#     vt100   6803  ok      console.sco    8954  ok
#     wyse50  7234  ok      color_console  9793  ok
#     h19     5014  ok      z29            7004  ok
#     ansi    9859  ->  "Insufficient memory / MEMORY ERROR!"
#
# 66 bytes separate color_console from ansi.  ansi is the largest entry in
# the file and the only one that does not fit.
#
# So TERM is passed through when Word can afford it, and substituted with
# vt100 when it cannot:
#
#   ansi   the description does not fit.  vt100 is an accurate description of
#          substrate's console either way -- it is VT100/ANSI compatible -- so
#          the substitution costs nothing in rendering.
#   linux  what substrate's login(1) sets, and termdesc has no such entry at
#          all: Word exits with "No termdesc entry for linux".
#
# XENIX_TERM overrides the choice outright, for a program with different
# needs or to test a specific description.
#
# Usage:  xenix /perso/xenix286s/usr/bin/word [args...]
#         XENIX_TERM=wyse50 xenix /perso/xenix286s/usr/bin/word

if [ $# -lt 1 ]; then
    echo "usage: xenix <program> [args...]" >&2
    echo "       XENIX_TERM=<term> overrides the terminal choice" >&2
    exit 64
fi

if [ -n "${XENIX_TERM:-}" ]; then
    _term=$XENIX_TERM
else
    case "${TERM:-}" in
    vt52|vt100|wyse50|console|console.sco|color_console|z29|h19)
        _term=$TERM ;;          # in termdesc and small enough to load
    *)
        _term=vt100 ;;          # ansi (too big), linux (absent), or unknown
    esac
fi
TERM=$_term
export TERM

# MSTOOLS programs write scratch files here and do not create it themselves.
# Path is relative to the personality root, so this is the Xenix /usr/tmp.
[ -d /perso/xenix286s/usr/tmp ] || mkdir -p /perso/xenix286s/usr/tmp 2>/dev/null

exec "$@"
