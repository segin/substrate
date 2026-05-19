#!/bin/sh
#
# contrib/qman/build.sh — build plp13/qman for substrate.
#
# WARNING: this build script is STAGED but does not yet succeed.
# qman's upstream build has several dependencies substrate has not
# yet ported.  Running this script today exits non-zero with the
# missing-dep list and a pointer at README.SUBSTRATE.md.  Once the
# blocking ports land, switch USE_STUB=0 below.
#
# Blockers (in dependency order):
#
#   1.  meson(1) + ninja(1) — qman uses meson as its build system.
#       Substrate has no python port yet, so neither tool is
#       available.  Substitution path: hand-author a Makefile under
#       patches/ that mirrors src/meson.build's source list and
#       link line, then drive it from this build.sh instead of
#       calling meson.
#
#   2.  cogapp (cog.py) — qman's config.h / config.c are generated
#       by Python cog templates (config.h.cog / config.c.cog).
#       Without cog we have no config layer.  Substitution path:
#       run cog on the BUILD host during fetch (host has Python),
#       snapshot the generated files, ship them as a patch series
#       so the substrate-target build skips the codegen step.
#
#   3.  libbsd-overlay — Linux BSD-compat shim.  Substrate already
#       provides the BSD functions in libc, so we can stub libbsd
#       to nothing.  Add a patch that drops the dependency() call.
#
#   4.  zlib — qman opens gzip-compressed man pages.  Substrate has
#       no zlib port (contrib/zlib/ doesn't exist).  Either port
#       zlib or carry a patch that disables gzip support.
#
#   5.  liblzma — optional, qman gates xz support on detection.
#       Easy: leave undetected, qman falls back to non-xz reads.
#
#   6.  ncursesw — substrate's contrib/ncurses builds ncurses but
#       qman wants the wide-character variant.  Verify with
#       `ls ${STAGE1_PREFIX}/i386-unknown-substrate/lib/libncursesw*`.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.5.1"
TREE_DIR="${HERE}/build/qman-${VERSION}"

cat >&2 <<EOF
contrib/qman/build.sh: not yet wired.

This contrib is staged for a future port — the source has been
fetched and extracted, but the build is blocked on missing
substrate ports:

  - meson + ninja (no python port yet)
  - cogapp (Python codegen for config.{c,h})
  - libbsd-overlay (BSD compat shim — substrate libc covers it,
    needs a patch to drop the dependency() call)
  - zlib (no contrib/zlib yet — needed for .gz man pages)
  - ncursesw vs ncurses wide-char variant (verify)

See contrib/qman/README.SUBSTRATE.md for the substitution paths
worth attempting per blocker.

Source ready at: ${TREE_DIR}
EOF
exit 1
