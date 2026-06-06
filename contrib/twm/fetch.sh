#!/bin/sh
# contrib/twm/fetch.sh — download + verify + extract + patch twm.
#
# twm (Tab Window Manager) is the minimal reference X11 window manager.
# Autotools build; depends on the core X client libraries plus a yacc/lex
# parser for its ~/.twmrc config grammar (generated on the build host).
set -eu
VERSION="1.0.12"
TARBALL="twm-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/app/${TARBALL}"
SHA256="aaf201d4de04c1bb11eed93de4bee0147217b7bdf61b7b761a56b2fdc276afe4"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/twm-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}";
    else wget -O "${TARBALL}" "${URL}"; fi
fi
echo "==> Verifying ${TARBALL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
# Teach the bundled config.sub the substrate OS token.  twm 1.0.12 ships a
# 2018-vintage config.sub (dashless OS whitelist, `sortix*`); also cover the
# older leading-dash form for safety.
for cs in $(find "${TREE_DIR}" -name config.sub); do
    grep -q 'substrate' "$cs" && continue
    if grep -q '\-sortix' "$cs"; then          # 2018 vintage: leading-dash OS list
        sed -i 's/\(-sortix\* \)/\1| -substrate* /' "$cs"
    elif grep -q 'sortix' "$cs"; then           # newer: dashless OS list
        sed -i 's/\(sortix\* \)/\1| substrate* /' "$cs"
    else                                        # fallback
        sed -i 's/\(-aros\* \)/\1| -substrate* /' "$cs"
    fi
done
if [ -f "${HERE}/series" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        [ -f "${HERE}/patches/${p}" ] || { echo "fetch.sh: missing patch ${p}" >&2; exit 1; }
        if patch -p1 --dry-run < "${HERE}/patches/${p}" >/dev/null 2>&1; then
            echo "==> applying ${p}"; patch -p1 < "${HERE}/patches/${p}"
        else echo "==> ${p} already applied (skipping)"; fi
    done < "${HERE}/series"
fi
echo "==> twm ${VERSION} ready at ${TREE_DIR}"
