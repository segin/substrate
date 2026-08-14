#!/bin/sh
# contrib/psymp3/fetch.sh — clone PsyMP3 pinned to a release tag, apply patches.
set -eu
REPO="https://github.com/segin/psymp3"
# Pinned to a release tag.  Bump REF to move the port; both substrate patches
# in series apply unchanged at this revision.
REF="1.99.16-RELEASE"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/psymp3"
mkdir -p "${BUILD_DIR}"
if [ ! -d "${TREE}/.git" ]; then
    git clone "${REPO}" "${TREE}"
fi
cd "${TREE}"
# A bare SHA fetch is refused by some servers, so fetch the branch (and tags)
# and resolve the ref out of that.
git fetch --all --tags 2>/dev/null || git fetch origin
git cat-file -e "${REF}^{commit}" 2>/dev/null || git fetch --all --tags
git checkout -f "${REF}"
git clean -fdx >/dev/null 2>&1 || true
if [ -f "${HERE}/series" ]; then
    while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
        echo "==> applying $p"; git apply "${HERE}/patches/$p"; done < "${HERE}/series"
fi
echo "PsyMP3 checked out at ${REF} ($(git rev-parse --short HEAD))"
