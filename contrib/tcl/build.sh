#!/bin/sh
#
# contrib/tcl/build.sh — cross-build core Tcl 8.6 for substrate.
#
# Builds the core only — libtcl8.6.a (PIC, so CDE's PIE binaries can link
# it), tclsh, the headers, the script library and tclConfig.sh.  The
# bundled optional packages (sqlite3, tdbc, thread, ...) are skipped: CDE
# only needs core Tcl, and sqlite3 wants alloca() which substrate's libc
# does not expose.
#
# Cross note: Tcl's build runs a NATIVE_TCLSH (the build host's tclsh) to
# generate sources, so the host needs tclsh installed; the target objects
# are produced by the cross compiler.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-tcl)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="8.6.16"
UNIX="${HERE}/build/tcl${VERSION}/unix"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-tcl}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH

[ -d "${UNIX}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
command -v tclsh >/dev/null 2>&1 || command -v tclsh8.6 >/dev/null 2>&1 || {
    echo "build.sh: a host tclsh is required (NATIVE_TCLSH) — install Tcl on the build host" >&2; exit 1; }

cd "${UNIX}"

# Teach the bundled config.sub about substrate (idempotent).
for cs in config.sub ../config.sub; do
    [ -f "$cs" ] && ! grep -q 'substrate\*' "$cs" && \
        sed -i 's/\(| sortix\* \)/\1| substrate* /' "$cs"
done

echo "==> configure"
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --disable-shared \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fPIC"

echo "==> make (core only) -j${JOBS}"
make -j"${JOBS}" binaries libraries

echo "==> install core into ${DESTDIR}"
rm -rf "${DESTDIR}"
make install-binaries install-libraries install-headers install-private-headers \
    DESTDIR="${DESTDIR}"

# CDE / scripts invoke plain `tclsh`.
if [ -f "${DESTDIR}/usr/bin/tclsh8.6" ]; then
    ln -sf tclsh8.6 "${DESTDIR}/usr/bin/tclsh"
fi

echo "==> Done.  core Tcl ${VERSION} staged under ${DESTDIR}"
