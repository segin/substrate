#!/bin/sh
#
# fix-termdesc-ansi.sh - repair the shipped "ansi" entry in MSTOOLS termdesc.
#
# Microsoft Word 3.0 refuses to start under TERM=ansi with
#
#     Insufficient memory
#     MEMORY ERROR!
#
# which is not a memory shortage at all -- it is what Word prints when the
# terminal description it loaded is unusable.  The entry as shipped is
# damaged in two ways:
#
#   1. The value column is missing from every =BOOLEAN= line.  The format is
#      <keyword> <value> <description> with the value at column 30, and the
#      entry instead reads
#
#          =BOOLEAN=am                 Terminal     has automatic margins
#
#      so "Terminal" is parsed as the value of am.  T and F are the only
#      values any other entry in the file uses; these four lines and two in
#      the z29 entry are the only exceptions in all ten entries.  The
#      description on the im line is character-for-character the one the
#      console/console.sco/color_console entries carry beside "F", so this
#      entry was copied from that family with the column dropped.  The
#      header line was mangled by the same edit.
#
#   2. The entry is missing the twelve attribute capabilities (ib/ie, Cb/Ce,
#      Sb/Se, SB/SE, xs/xe, db/de) that termdesc's own format documentation
#      marks "ALL SHOULD APPEAR FOR EVERY TERMINAL ENTRY".  This is what
#      produces the memory error: restoring any two properly columned
#      capability lines is enough to clear it, and the malformed booleans
#      alone are survivable (z29 has them and works).  Column alignment
#      matters -- the same two capabilities added with single-space
#      separators do not register.
#
#   3. The entry has none of the graphics capabilities (G1-G4, GB, GC, GD,
#      GE, GH, GI, GJ, GK, GL, GM, GN, GP, GR, GS, GT, GU, GV, GW, GZ) that
#      Word emits when it draws its window frame, ruler and marks.  Fixing
#      (1) and (2) alone gets Word as far as painting its banner and then
#
#          SYSTEM ERROR : Segment Violation!!
#
#      Fourteen of these appear in all nine other entries in the file and in
#      none of ansi -- the only capabilities of which that is true.  Adding
#      them clears the fault.
#
# The replacement entry fixes all three and leaves every other entry in the
# file untouched.
#
# Attributes use ANSI SGR.  Substrate's console implements SGR 0-8, 22-28 and
# 30-47 and ignores unknown parameters, so xs (\E[9m) and db (\E[21m) degrade
# to plain text rather than corrupting the display.
#
# The graphics characters are the plain ASCII ones this file documents as the
# defaults (+ - | . etc.), and GS/GE -- graphics mode start/end, the two codes
# the documentation gives no default for -- are deliberately left out, because
# a strictly ANSI terminal has no graphics mode to enter.  Copying vt100's
# block instead also clears the fault, but vt100 drives its line drawing with
# SO/SI charset shifting, and on substrate's console that leaves the terminal
# in DEC special graphics: under TERM=vt100 Word renders its own banner as
# "Microsof|- Word 3.00" / "A|`|` Righ|-s Reser-'ed".  The ASCII set renders
# correctly here and degrades safely on terminals with no alternate charset.
#
# Usage:  fix-termdesc-ansi.sh <path-to-termdesc>
#
# Idempotent: re-running on an already-fixed file is a no-op.  termdesc
# lives inside xenix286s.img, which is not tracked, so extract it, run this,
# and write it back:
#
#     debugfs -R 'dump /usr/lib/MSTOOLS/termdesc td' xenix286s.img
#     bin/xenix/fix-termdesc-ansi.sh td
#     debugfs -w xenix286s.img <<'EOF'
#     cd /usr/lib/MSTOOLS
#     rm termdesc
#     write td termdesc
#     sif termdesc mode 0100644
#     EOF

set -e

td=$1
if [ -z "$td" ] || [ ! -f "$td" ]; then
    echo "usage: $0 <path-to-termdesc>" >&2
    exit 64
fi

entry=$(dirname "$0")/termdesc-ansi.entry
if [ ! -f "$entry" ]; then
    echo "$0: cannot find $entry" >&2
    exit 66
fi

if ! grep -q '^ansi|' "$td"; then
    echo "$0: $td has no 'ansi' entry -- is this an MSTOOLS termdesc?" >&2
    exit 65
fi

# Entries start at column 1; the ansi entry runs to the next such line.
awk -v repl="$entry" '
    /^[a-zA-Z0-9]/ { inansi = ($0 ~ /^ansi\|/) }
    inansi { if (!done) { while ((getline l < repl) > 0) print l; print ""; done = 1 } next }
    { print }
' "$td" > "$td.new"

if cmp -s "$td" "$td.new"; then
    rm -f "$td.new"
    echo "$td: already correct, nothing to do"
    exit 0
fi

mv "$td.new" "$td"

# The entry is worthless if the value column drifted, so assert it landed at
# column 30 the way every other entry in the file has it.
bad=$(awk '
    /^[a-zA-Z0-9]/ { e = ($0 ~ /^ansi\|/) }
    e && /=(BOOLEAN|OUTPUT|NUMBER)=/ {
        base = ($0 ~ /BOOLEAN/) ? 13 : 12
        if (match(substr($0, base), /[^ ]/) + base - 1 != 30) print
    }' "$td")
if [ -n "$bad" ]; then
    echo "$0: value column is not 30 on:" >&2
    echo "$bad" >&2
    exit 1
fi

echo "$td: ansi entry replaced ($(grep -c . "$entry") lines, all values at column 30)"
