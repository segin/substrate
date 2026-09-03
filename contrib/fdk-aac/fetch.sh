#!/bin/sh
# contrib/fdk-aac/fetch.sh — Fraunhofer FDK AAC codec library.
# Fetch, verify, extract, patch.
#
# The tarball is the `make dist` release from the opencore-amr project on
# SourceForge, which is upstream's distribution point for fdk-aac releases.
# Deliberately NOT github.com/mstorsjo/fdk-aac/archive/: those are raw tree
# snapshots with no generated configure, and GitHub does not promise their
# bytes are stable over time.
set -eu

VER="2.0.3"
TB="fdk-aac-${VER}.tar.gz"
URL="https://downloads.sourceforge.net/opencore-amr/${TB}"
URL_FALLBACK="https://sourceforge.net/projects/opencore-amr/files/fdk-aac/${TB}/download"
SHA256="829b6b89eef382409cda6857fd82af84fabb63417b08ede9ea7a553f811cb79e"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE="${BUILD_DIR}/fdk-aac-${VER}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TB}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    for u in "${URL}" "${URL_FALLBACK}"; do
        echo "==> Fetching ${u}"
        if command -v curl >/dev/null 2>&1; then
            curl -fSL -o "${TB}" "${u}" && break
        else
            wget -O "${TB}" "${u}" && break
        fi
    done
fi
[ -f "${TB}" ] || { echo "fetch.sh: could not download ${TB}" >&2; exit 1; }

echo "==> Verifying ${TB}"
echo "${SHA256}  ${TB}" | sha256sum -c -

# Re-extract every time: a tree left over from a previous run is how a port
# ends up depending on edits nobody recorded.
echo "==> Extracting"
rm -rf "${TREE}"
tar xzf "${TB}"

if [ -s "${HERE}/series" ]; then
    echo "==> Applying patch series"
    cd "${TREE}"
    while IFS= read -r p || [ -n "${p}" ]; do
        case "${p}" in ''|\#*) continue;; esac
        echo "    - ${p}"
        patch -p1 -F0 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> fdk-aac ${VER} ready at ${TREE}"
