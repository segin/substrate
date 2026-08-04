#!/bin/sh
# contrib/psymp3/fetch.sh — clone PsyMP3 pinned to a release tag, apply patches.
set -eu
REPO="https://github.com/segin/psymp3"
# Pinned to a commit rather than a release tag: 1.99.11-RELEASE is 155 commits
# behind master, and the port tracks upstream development.  Bump COMMIT to move
# it; both substrate patches in series apply unchanged at this revision.
COMMIT="63cb0f56"    # Codecs: Fix undefined negative left-shifts in PCM sample conversion
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/psymp3"
mkdir -p "${BUILD_DIR}"
if [ ! -d "${TREE}/.git" ]; then
    git clone "${REPO}" "${TREE}"
fi
cd "${TREE}"
# A bare SHA fetch is refused by some servers, so fetch the branch (and tags)
# and resolve the commit out of that.
git fetch --all --tags 2>/dev/null || git fetch origin
git cat-file -e "${COMMIT}^{commit}" 2>/dev/null || git fetch --all --tags
git checkout -f "${COMMIT}"
git clean -fdx >/dev/null 2>&1 || true
if [ -f "${HERE}/series" ]; then
    while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
        echo "==> applying $p"; git apply "${HERE}/patches/$p"; done < "${HERE}/series"
fi
echo "PsyMP3 checked out at ${COMMIT} ($(git rev-parse --short HEAD))"
