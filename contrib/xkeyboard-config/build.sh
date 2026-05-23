#!/bin/sh
# contrib/xkeyboard-config/build.sh — stage the xkeyboard-config data
# tree under dist-xkeyboard-config/usr/share/X11/xkb/.
#
# No actual build step — the rules / symbols / keycodes / types /
# compat / geometry trees are plain text that xkbcomp consumes at
# runtime.  The upstream meson build also runs gettext/msgfmt over
# the .po files; we skip that (locale-translated layout descriptions
# aren't required for the server to function).

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.36"
TREE_DIR="${HERE}/build/xkeyboard-config-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-xkeyboard-config}"

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

XKB_DEST="${DESTDIR}/usr/share/X11/xkb"
echo "==> staging XKB data into ${XKB_DEST}"
rm -rf "${DESTDIR}"
mkdir -p "${XKB_DEST}"

for sub in rules symbols keycodes types compat geometry; do
    [ -d "${TREE_DIR}/${sub}" ] || continue
    cp -a "${TREE_DIR}/${sub}" "${XKB_DEST}/"
done

# Assemble the `base` and `evdev` rules files from the .part fragments
# shipped by xkeyboard-config.  The upstream meson build does this; we
# don't have cross-meson, so reproduce the concat sequence here.  Each
# parts list is concatenated in order to produce one output file; the
# @0@ token in the part names is substituted with the variant ('base'
# or 'evdev').
assemble_rules() {
    variant="$1"
    out="${XKB_DEST}/rules/${variant}"
    rdir="${XKB_DEST}/rules"
    # Order taken verbatim from rules/meson.build's `parts` list.
    for p in \
        0000-hdr.part \
        0001-lists.part \
        "0002-${variant}.lists.part" \
        compat/0003-lists.part \
        "0004-${variant}.m_k.part" \
        0005-l1_k.part \
        0006-l_k.part \
        0007-o_k.part \
        0008-ml_g.part \
        0009-m_g.part \
        0011-mlv_s.part \
        0013-ml_s.part \
        0015-ml1_s.part \
        0018-ml2_s.part \
        0020-ml3_s.part \
        0022-ml4_s.part \
        "0026-${variant}.m_s.part" \
        "0027-${variant}.ml_s1.part" \
        compat/0028-lv_c.part \
        compat/0029-l1v1_c.part \
        compat/0030-l2v2_c.part \
        compat/0031-l3v3_c.part \
        compat/0032-l4v4_c.part \
        0033-ml_c.part \
        0034-ml1_c.part \
        0035-m_t.part \
        0036-lo_s.part \
        0037-l1o_s.part \
        0038-l2o_s.part \
        0039-l3o_s.part \
        0040-l4o_s.part \
        compat/0041-o_s.part \
        0042-o_s.part \
        0043-o_c.part \
        0044-o_t.part \
    ; do
        [ -f "${rdir}/${p}" ] && cat "${rdir}/${p}" || true
    done > "${out}"
}

echo "==> Assembling rules/base and rules/evdev from .part fragments"
assemble_rules base
assemble_rules evdev

# The rules/ dir ships .xml / .lst / shell-fragment "base" rule files
# we want to keep, but also Makefile.am / meson.build noise that
# shouldn't ship on the rootfs.  Trim build-system bits.
find "${XKB_DEST}" \( -name 'Makefile.am' -o -name 'meson.build' -o \
                      -name 'Makefile.in' -o -name 'meson_options.txt' \) -delete

echo "==> Done.  xkeyboard-config staged at ${XKB_DEST}"
echo "    size: $(du -sh "${XKB_DEST}" | awk '{print $1}')"
