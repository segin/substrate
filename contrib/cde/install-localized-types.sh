#!/bin/sh
# install-localized-types.sh — stage the C-locale CDE datatype/action database
# and the dtwm Front Panel definition into dist-cde.
#
# CDE's `localized` and `types` build clusters are skipped in build.sh (their
# message-catalog generators — merge/mkcatdefs — don't cross-build to the
# substrate target).  As a side effect the %|nls-N-#tag#| placeholders in
# programs/types/*.dt and dtwm.fp are never expanded and the whole
# /usr/dt/appconfig/types tree is never installed.  Without it dtwm has no
# Front Panel definition (so it draws no panel) and the datatype/action
# database is empty.
#
# This script reproduces just the C-locale slice of `localized`: it expands the
# placeholders against the C message catalog (programs/localized/C/types/
# _common.dt.tmsg, .../action.tmsg) using cdemerge.py — a faithful replica of
# programs/localized/util/merge.c — and installs the canonical DTTYPES set
# (the list from programs/localized/templates/types.am, which deliberately
# excludes template/vendor files like printerNN.dt that aren't valid stand-alone
# .dt databases) plus dtwm.fp.  It does NOT touch sys.dtwmrc: the cpp'd copy the
# main install already stages is correct (its leftover %|nls| lines are
# non-fatal warnings), whereas a merged copy mis-expands the menu sections.
#
# Usage: install-localized-types.sh <cde-source-tree> <DESTDIR>
set -e

CDESRC="$1"      # .../build/cdesktopenv/cde
DESTDIR="$2"     # .../dist-cde
HERE="$(cd "$(dirname "$0")" && pwd)"

TYPES="${CDESRC}/programs/types"
TCAT="${CDESRC}/programs/localized/C/types"
TYPESAM="${CDESRC}/programs/localized/templates/types.am"
OUT="${DESTDIR}/usr/dt/appconfig/types/C"

if [ ! -d "${TYPES}" ] || [ ! -f "${TCAT}/_common.dt.tmsg" ]; then
    echo "  (localized types sources absent — skipping Front Panel install)"
    exit 0
fi

mkdir -p "${OUT}"

# Canonical install list — DTTYPES (multi-line, backslash-continued).
DTTYPES=$(sed -n '/^DTTYPES[ 	]*=/,/[^\\]$/p' "${TYPESAM}" \
          | tr -d '\\' | tr -s ' 	' '\n' | grep -E '\.(dt|fp)$' || true)

for b in ${DTTYPES}; do
    [ -f "${TYPES}/${b}" ] || continue
    python3 "${HERE}/cdemerge.py" "${TCAT}/_common.dt.tmsg" "${TYPES}/${b}" \
        > "${OUT}/${b}"
done

# dtwm.fp is the Front Panel definition; action is the action header.
[ -f "${TYPES}/dtwm.fp" ] && \
    python3 "${HERE}/cdemerge.py" "${TCAT}/_common.dt.tmsg" "${TYPES}/dtwm.fp" \
        > "${OUT}/dtwm.fp"
[ -f "${TYPES}/action" ] && [ -f "${TCAT}/action.tmsg" ] && \
    python3 "${HERE}/cdemerge.py" "${TCAT}/action.tmsg" "${TYPES}/action" \
        > "${OUT}/action"

# The locale dir is a symlink to C (install-data-hook in
# programs/localized/C/types/Makefile.am).  dtwm looks up the Front Panel by
# $LANG (en_US.UTF-8), falling back to C.
ln -sfn C "${DESTDIR}/usr/dt/appconfig/types/en_US.UTF-8"

# ToolTalk process-type SOURCES.  The `tttypes` cluster is deferred (its
# build compiles the .ptype files with the cross-built tt_type_comp, which
# can't run here), but the artifacts CDE installs into appconfig/tttypes are
# the .ptype text files themselves — the runtime type DB is compiled on the
# target.  Stage them by copy.
if [ -d "${CDESRC}/programs/tttypes" ]; then
    mkdir -p "${DESTDIR}/usr/dt/appconfig/tttypes"
    cp -f "${CDESRC}/programs/tttypes/"*.ptype "${DESTDIR}/usr/dt/appconfig/tttypes/"
fi

echo "  installed $(ls "${OUT}" 2>/dev/null | wc -l) datatype/action files + dtwm.fp Front Panel"
