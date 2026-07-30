#!/bin/sh
# build.sh — build GRUB for every platform the Substrate image build needs,
# and stage it under contrib/grub/dist-grub/usr.
#
# This is a HOST-tool port, unlike the rest of contrib/.  GRUB here is not
# Substrate userland: it is the bootloader that loads the Substrate kernel,
# plus the host utilities that assemble it into rootfs.img.  Nothing built
# here runs under Substrate, so it uses the plain host compiler and NOT the
# i386-unknown-substrate cross toolchain.
#
# GRUB configures for exactly one target platform per pass, so this runs one
# pass per platform.  The three platforms are the ones build-rootfs.sh's
# install_grub() and mkgrub.sh actually consume:
#
#   i386-pc      BIOS boot: boot.img (embedded in the MBR by
#                tools/grub-embed-mbr) + core.img + the *.mod tree
#   x86_64-efi   64-bit UEFI: BOOTX64.EFI built by grub-mkimage
#   i386-efi     32-bit UEFI: BOOTIA32.EFI, for 32-bit firmware
#
# Each pass installs into the same prefix.  Platform modules land in
# distinct $prefix/lib/grub/<target>-<platform>/ trees, and the host
# utilities (grub-mkimage, grub-file, grub-mkrescue, ...) are identical
# across passes, so the later passes simply overwrite them.
#
# Usage:
#   ./fetch.sh && ./build.sh              # all three platforms
#   ./build.sh i386-pc                    # just one
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.12"
TREE_DIR="${HERE}/build/grub-${VERSION}"
STAGE="${HERE}/dist-grub/usr"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# grub-core/extra_deps.lst is referenced by the generated Makefile:
#
#   syminfo.lst: gensyminfo.sh kernel_syms.lst $(top_srcdir)/grub-core/extra_deps.lst ...
#           cat kernel_syms.lst $(top_srcdir)/grub-core/extra_deps.lst > $@.new
#
# but the 2.12 release tarball does not ship it, so an out-of-tree build dies
# with "No rule to make target .../grub-core/extra_deps.lst".  It is a list of
# extra inter-module dependencies appended to kernel_syms.lst; we add none, so
# an empty file is the correct content.  Created here rather than as a patch
# because this is a missing *generated* file, not a defect in upstream source.
[ -f "${TREE_DIR}/grub-core/extra_deps.lst" ] || : > "${TREE_DIR}/grub-core/extra_deps.lst"

# Tools GRUB's build genuinely needs.  Checked up front: a missing bison or
# flex otherwise fails deep inside the build with an obscure message.
missing=
for t in bison flex python3 gcc make; do
    command -v "${t}" >/dev/null 2>&1 || missing="${missing} ${t}"
done
[ -n "${missing}" ] && { echo "build.sh: missing required tools:${missing}" >&2; exit 1; }

# i386 platforms are built with the host compiler in 32-bit mode.  GRUB is
# freestanding (-ffreestanding, no libc), so -m32 is sufficient and no 32-bit
# userland runtime is required -- but the compiler must still be able to emit
# 32-bit objects.
build_one() {
    platform="$1"
    case "${platform}" in
        i386-pc)     target=i386   ;  plat=pc  ; extra_cc="-m32" ;;
        i386-efi)    target=i386   ;  plat=efi ; extra_cc="-m32" ;;
        x86_64-efi)  target=x86_64 ;  plat=efi ; extra_cc=""     ;;
        *) echo "build.sh: unknown platform '${platform}'" >&2; return 1 ;;
    esac

    if [ -n "${extra_cc}" ]; then
        printf 'int main(void){return 0;}\n' > "${HERE}/.cc32test.c"
        if ! gcc ${extra_cc} -c "${HERE}/.cc32test.c" -o "${HERE}/.cc32test.o" 2>/dev/null; then
            rm -f "${HERE}/.cc32test.c" "${HERE}/.cc32test.o"
            echo "build.sh: gcc cannot emit 32-bit objects; skipping ${platform}" >&2
            echo "          (install the 32-bit toolchain support for your distro)" >&2
            return 1
        fi
        rm -f "${HERE}/.cc32test.c" "${HERE}/.cc32test.o"
    fi

    obj="${HERE}/build/obj-${platform}"
    echo "==> Configuring grub for ${platform} (${obj})"
    mkdir -p "${obj}"
    cd "${obj}"

    if [ ! -f config.status ]; then
        # --disable-werror: GRUB 2.12 does not build warning-clean on newer
        #   GCC, and these are upstream's warnings, not ours.
        # --disable-efiemu: a host-side EFI emulator we never use; it wants a
        #   32-bit *hosted* compile, which is a separate dependency.
        # --disable-grub-mkfont / --disable-grub-themes: pull in freetype and
        #   the theme pipeline for graphical menus we do not ship.
        # --disable-nls: no translations in the boot path.
        "${TREE_DIR}/configure" \
            --prefix="${STAGE}" \
            --target="${target}" \
            --with-platform="${plat}" \
            --disable-werror \
            --disable-efiemu \
            --disable-grub-mkfont \
            --disable-grub-themes \
            --disable-nls \
            TARGET_CC="gcc ${extra_cc}" \
            TARGET_CFLAGS="${extra_cc} -Os -ffreestanding" \
            TARGET_CPPFLAGS="${extra_cc}" \
            TARGET_CCASFLAGS="${extra_cc}" \
            TARGET_LDFLAGS="${extra_cc}" \
            >configure.log 2>&1 || {
                echo "build.sh: configure failed for ${platform}; see ${obj}/configure.log" >&2
                tail -20 configure.log >&2
                return 1
            }
    fi

    echo "==> Building ${platform}"
    make -j"${JOBS}" >build.log 2>&1 || {
        echo "build.sh: make failed for ${platform}; see ${obj}/build.log" >&2
        tail -30 build.log >&2
        return 1
    }

    echo "==> Staging ${platform} into ${STAGE}"
    make install >install.log 2>&1 || {
        echo "build.sh: install failed for ${platform}; see ${obj}/install.log" >&2
        tail -20 install.log >&2
        return 1
    }
    return 0
}

mkdir -p "${STAGE}"

if [ $# -gt 0 ]; then
    platforms="$*"
else
    platforms="i386-pc x86_64-efi i386-efi"
fi

failed=
for p in ${platforms}; do
    build_one "${p}" || failed="${failed} ${p}"
done

echo
echo "==> Staged under ${STAGE}"
for d in "${STAGE}"/lib/grub/*/; do
    [ -d "${d}" ] || continue
    printf '    %-14s %s modules\n' "$(basename "${d}")" \
        "$(find "${d}" -name '*.mod' 2>/dev/null | wc -l)"
done
echo "    utilities:"
for u in grub-mkimage grub-file grub-mkrescue; do
    if [ -x "${STAGE}/bin/${u}" ]; then
        printf '      %s\n' "${STAGE}/bin/${u}"
    else
        printf '      %s MISSING\n' "${u}"
    fi
done

if [ -n "${failed}" ]; then
    echo
    echo "build.sh: these platforms did not build:${failed}" >&2
    echo "          the image build falls back to the host GRUB for them." >&2
    exit 1
fi
