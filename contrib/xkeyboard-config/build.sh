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

# The rules/ dir ships .xml / .lst / shell-fragment "base" rule files
# we want to keep, but also Makefile.am / meson.build noise that
# shouldn't ship on the rootfs.  Trim build-system bits.
find "${XKB_DEST}" \( -name 'Makefile.am' -o -name 'meson.build' -o \
                      -name 'Makefile.in' -o -name 'meson_options.txt' \) -delete

echo "==> Done.  xkeyboard-config staged at ${XKB_DEST}"
echo "    size: $(du -sh "${XKB_DEST}" | awk '{print $1}')"
