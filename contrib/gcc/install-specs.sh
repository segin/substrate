#!/bin/sh
#
# contrib/gcc/install-specs.sh — install the cross toolchain's `specs` override.
#
# The cross g++/gcc needs two extra linker flags on every link that the
# built-in specs do not carry:
#
#   --copy-dt-needed-entries  the ports link against .so files whose own
#                             DT_NEEDED deps are not repeated on the command
#                             line (ld >= 2.22 stopped following those).
#   -rpath-link <sysroot>/lib so ld can resolve those transitive deps out of
#                             the cross sysroot at link time.
#
# GCC reads `<libdir>/specs` after its built-in specs, and a spec body starting
# with "+ " is *appended* to the built-in one.  The catch: that append is
# concatenated against `link_spec` as it exists *before* gcc prepends
# LINK_EH_SPEC to it.  So a naive
#
#     *link:
#     + --copy-dt-needed-entries -rpath-link ...
#
# silently drops the `--eh-frame-hdr` that
# patches/0010-libgcc-pt-gnu-eh-frame-substrate.patch adds via LINK_EH_SPEC:
# the built-in LINK_SPEC part (-m elf_i386_substrate ...) survives, the
# LINK_EH_SPEC prefix does not.  Without it ld emits no .eh_frame_hdr section
# and no PT_GNU_EH_FRAME segment, libgcc's dl_iterate_phdr-based unwinder can
# find no FDEs for that module, and *every* C++ throw from the main executable
# reaches std::terminate instead of its catch — even a plain
# `try { ... } catch (...) {}` in the same file.
#
# So the override has to re-state --eh-frame-hdr itself.  Keep it here, and
# keep it %{!static:...}-guarded, exactly as LINK_EH_SPEC does.
#
# Env: STAGE1_PREFIX (default /opt/substrate), GCC_VERSION (default 16.1.0).

set -eu

: "${STAGE1_PREFIX:=/opt/substrate}"
: "${GCC_VERSION:=16.1.0}"

TARGET="i386-unknown-substrate"
SR="${STAGE1_PREFIX}/${TARGET}"
LIBDIR="${STAGE1_PREFIX}/lib/gcc/${TARGET}/${GCC_VERSION}"

[ -d "${LIBDIR}" ] || {
    echo "install-specs.sh: no such gcc libdir: ${LIBDIR}" >&2
    exit 1
}

cat > "${LIBDIR}/specs" <<EOF
*link:
+ %{!static:--eh-frame-hdr} --copy-dt-needed-entries -rpath-link ${SR}/lib
EOF

echo "==> installed ${LIBDIR}/specs"
