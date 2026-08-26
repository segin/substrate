#!/bin/sh
#
# xenix - run a SCO Xenix/286 program with an environment MSTOOLS understands.
#
# Xenix binaries run natively under the PERS_SCO_X286 personality, so this is
# not an emulator wrapper -- it exists purely to fix up the environment, and
# TERM is the whole reason.
#
# Microsoft Word 3.0 looks TERM up in /usr/lib/MSTOOLS/termdesc and dies if it
# is not there:
#
#     No termdesc entry for linux
#
# and substrate's login(1) sets TERM=linux, which that file has never heard
# of.  So TERM is passed through when termdesc actually describes it, and
# otherwise replaced with vt100 -- an accurate description of substrate's
# VT100/ANSI-compatible console, and present in every copy of the file.
#
# Asking termdesc directly rather than carrying a list here means a terminal
# stays supported for exactly as long as the file describes it.  TERM=ansi in
# particular used to fail with a bogus "Insufficient memory / MEMORY ERROR!";
# that was a damaged termdesc entry, repaired by fix-termdesc-ansi.sh in this
# directory, and it now works with no special case here.
#
# XENIX_TERM overrides the choice outright, for a program with different needs
# or to test a specific description.
#
# Usage:  xenix /perso/xenix286s/usr/bin/word [args...]
#         XENIX_TERM=wyse50 xenix /perso/xenix286s/usr/bin/word

XENIX_ROOT=${XENIX_ROOT:-/perso/xenix286s}
TERMDESC=$XENIX_ROOT/usr/lib/MSTOOLS/termdesc

if [ $# -lt 1 ]; then
    echo "usage: xenix <program> [args...]" >&2
    echo "       XENIX_TERM=<term> overrides the terminal choice" >&2
    exit 64
fi

if [ -n "${XENIX_TERM:-}" ]; then
    _term=$XENIX_TERM
else
    _term=vt100
    # Entry names start at column 1 and end at '|'.  Restricted to plain
    # names so nothing in TERM reaches grep as a pattern.
    case "${TERM:-}" in
    *[!A-Za-z0-9._-]* | '')
        ;;
    *)
        if [ -r "$TERMDESC" ] && grep -q "^${TERM}|" "$TERMDESC" 2>/dev/null; then
            _term=$TERM
        fi
        ;;
    esac
fi
TERM=$_term
export TERM

# MSTOOLS programs write scratch files here and do not create it themselves.
[ -d "$XENIX_ROOT/usr/tmp" ] || mkdir -p "$XENIX_ROOT/usr/tmp" 2>/dev/null

exec "$@"
