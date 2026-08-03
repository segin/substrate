#!/bin/sh
#
# contrib/cde/build.sh — cross-build the Common Desktop Environment for
# substrate and stage it into dist-overlay/dist-cde.
#
# The hard part of cross-building CDE is not the compiler: it is that CDE
# builds a couple of dozen small programs and then RUNS them, mid-build, to
# generate source, message catalogs, ToolTalk type databases and help
# volumes.  Cross-compiled, none of those can execute on the build host.
#
# The port solves that in one move rather than a pile of special cases.
# hosttools/build.sh builds a complete NATIVE objdir of the same CDE tree
# (hosttools/cde-host), and this script points CDE's own generator variables
# at it.  Every generator is reachable as
#
#     $(CDE_HOST)/$(subdir)/<tool>
#
# because automake defines `subdir` in every Makefile, so one set of
# command-line variables redirects every generator to its native twin, in
# every directory, without copying binaries into the cross tree or racing
# make's timestamps.  Where a generator's path is not already a variable
# upstream, the patch series makes it one.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
TREE_DIR="${HERE}/build/cdesktopenv/cde"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

HOSTTOOLS="${HERE}/hosttools/prefix/bin"
CDE_HOST="${HERE}/hosttools/cde-host"
DESTDIR="${SUBSTRATE_TOP}/dist-overlay/dist-cde"
SR="${HERE}/build/sysroot"

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Build-host programs CDE's configure requires, plus the native objdir.
[ -x "${HOSTTOOLS}/rpcgen" ] && [ -f "${CDE_HOST}/.substrate-hostbuild-done" ] || \
    ( cd "${HERE}/hosttools" && ./build.sh )
PATH="${STAGE1_PREFIX}/bin:${HOSTTOOLS}:${PATH}"; export PATH

# --- sysroot ---------------------------------------------------------------
# Motif, the X client stack and the handful of libraries CDE links, merged
# into one tree so configure's link tests and the compile lines see a single
# -I/-L pair.
echo "==> assembling sysroot at ${SR}"
rm -rf "${SR}"; mkdir -p "${SR}/usr/lib"
_have=0
for d in xorgproto libXau xtrans libxcb libX11 libXext libICE libSM \
         libXt libXmu libXpm libXaw libXinerama libXScrnSaver \
         libjpeg lmdb tcl libtirpc zlib motif; do
    st="${SUBSTRATE_TOP}/dist-overlay/dist-${d}"
    [ -d "${st}/usr" ] || { echo "    missing dist-${d}"; continue; }
    cp -a "${st}/usr/." "${SR}/usr/"
    _have=$((_have + 1))
done
[ "${_have}" -ge 20 ] || {
    echo "build.sh: only ${_have}/20 prerequisite dist trees found — build the X stack, Motif, libjpeg, lmdb, Tcl, libtirpc and zlib first" >&2
    exit 1
}
for l in c sys m pthread dl; do
    cp "${SUBSTRATE_TOP}/lib/${l}/lib${l}.so.0" "${SR}/usr/lib/" 2>/dev/null || true
    # Unversioned link name: -l<x> does not find lib<x>.so.0 on its own, and
    # without it ld reports "DSO missing from command line" for anything
    # reached only through a DT_NEEDED chain.
    ln -sf "lib${l}.so.0" "${SR}/usr/lib/lib${l}.so"
done

# --- dtksh: can we build it? -----------------------------------------------
# dtksh is the one program whose build is not CDE's own.  It drives AST's
# package/mamake system over the bundled ksh93, which compiles feature probes
# with iffe and then RUNS them — so ksh93's FEATURE headers describe whatever
# machine executed the probe.  Cross-compiling, that has to be substrate, via
# the qemu-backed crossexec harness hosttools installs.  Without a baked
# rootfs.img to derive the exec image from there is no way to run anything on
# the target, and dtksh is configured out rather than silently built for the
# build host's architecture.
#
# Set CDE_DISABLE_DTKSH=1 to leave it out regardless.  As of this writing that
# is the default state of affairs — see README.SUBSTRATE.md, "dtksh": ksh93
# builds, but only without the SHOPT set dtksh's own objects are compiled
# with, and the resulting libshell is missing symbols dtksh references.
EXEC_IMG="${SUBSTRATE_EXEC_IMG:-${HERE}/hosttools/build/sub-exec.img}"
if [ -n "${CDE_DISABLE_DTKSH:-}" ]; then
    DTKSH_ARG=--disable-dtksh
    echo "build.sh: dtksh disabled by CDE_DISABLE_DTKSH" >&2
elif [ -x "${HOSTTOOLS}/crossexec" ] && [ -f "${EXEC_IMG}" ]; then
    DTKSH_ARG=
else
    DTKSH_ARG=--disable-dtksh
    echo "build.sh: NOTE: no crossexec exec image (${EXEC_IMG}) — dtksh disabled." >&2
    echo "build.sh:       Bake a rootfs.img, re-run hosttools/build.sh, then rebuild." >&2
fi

# --- configure -------------------------------------------------------------
# Build IN-SOURCE: CDE's Makefiles reference its exported Dt/* headers as
# -I../../include relative to the build directory, and those headers live in
# the source tree, so an out-of-source build cannot find them.
cd "${TREE_DIR}"

# The -Wno-error= set is about GCC 16, not about substrate: this is 30-year-old
# C that predates most of what the compiler now rejects by default.  The
# substrate-specific part (the Linux code paths, the feature-test macros) is in
# configure.ac, via the patch series.
echo "==> configure"
#
# --disable-docs: the doc/ tree runs dtdocbook2man, which drives the freshly
# built (target) dtdocbook and instant binaries over CDE's SGML sources.  Those
# are programs, not generators with an overridable path, so there is nothing to
# redirect at the native objdir.  The manual pages are not part of the desktop
# runtime.
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr/dt \
    --disable-docs ${DTKSH_ARG} \
    CC=i386-unknown-substrate-gcc \
    CXX=i386-unknown-substrate-g++ \
    CPPFLAGS="-Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=return-mismatch -Wno-error=format -I${SR}/usr/include -I${SR}/usr/include/X11 -I${SR}/usr/include/tirpc" \
    LDFLAGS="-L${SR}/usr/lib -Wl,-rpath-link,${SR}/usr/lib" \
    --with-tcl="${SR}/usr/lib"

# --- generator redirection -------------------------------------------------
# Single-quoted: $(CDE_HOST) and $(subdir) must reach make unexpanded so each
# Makefile resolves them against its own directory.
set -- \
    CDE_HOST="${CDE_HOST}" \
    GENCPP="${HOSTTOOLS}/tradcpp" \
    LINETODATA='$(CDE_HOST)/$(subdir)/../util/lineToData' \
    MK_FONTS_ALIAS='$(CDE_HOST)/$(subdir)/mk_fonts_alias' \
    MKDBD='$(CDE_HOST)/$(subdir)/mkdbd' \
    ELTDEF='$(CDE_HOST)/$(subdir)/eltdef' \
    HELPBUILD='$(CDE_HOST)/$(subdir)/build' \
    CONTEXT='$(CDE_HOST)/$(subdir)/../util/context' \
    PMAKER="${CDE_HOST}/programs/dtinfo/tools/misc/pmaker" \
    DFILES="${CDE_HOST}/programs/dtinfo/tools/misc/dfiles" \
    MSGSETS="${CDE_HOST}/programs/dtinfo/tools/misc/msgsets" \
    TREERES="${CDE_HOST}/programs/dtinfo/tools/misc/treeres" \
    MERGE="${CDE_HOST}/programs/localized/util/merge" \
    MKCATDEFS="${CDE_HOST}/programs/localized/util/mkcatdefs" \
    TT_TYPE_COMP="${CDE_HOST}/lib/tt/bin/tt_type_comp/tt_type_comp" \
    TCL_INCLUDE_SPEC="-I${SR}/usr/include" \
    TCL_LIB_SPEC="-L${SR}/usr/lib -ltcl8.6"

# dtcodegen generates dtappbuilder's and ttsnoop's *_ui.c/_ui.h.  It links
# Motif, so the native objdir can only have built it if the build host has a
# Motif of its own.  Keep the NLSPATH prefix the Makefiles set.
DTCG="${CDE_HOST}/programs/dtappbuilder/src/abmf/dtcodegen"
if [ -x "${DTCG}" ]; then
    set -- "$@" DTCODEGEN="\$(DTCODEGENCAT) ${DTCG}"
else
    echo "build.sh: NOTE: no native dtcodegen — dtappbuilder and ttsnoop will not build" >&2
fi

# substrate splits the raw syscall wrappers (setsid, ...) into libsys, which
# the CDE static libraries reference.  libc.so DT_NEEDEDs libsys, but ld will
# not resolve through a transitive DSO, so every executable links -lsys
# directly.  -lstdc++ pulls the shared C++ runtime that the C++ objects inside
# libDtSvc/libDtMail need, which the C-driver links (dtpad and friends) drag
# out of the static archives too.  substrate's libstdc++.so has hard
# references to the pthread API but no DT_NEEDED on libpthread, so -lpthread
# has to follow it explicitly or every C++-touching link fails on
# pthread_mutex_init and friends.  configure sets LIBS to "-ldl -lm".
# -lz: dtdocbook's `instant` links Tcl, whose zlib channel wants zlibVersion.
set -- "$@" LIBS="-ldl -lm -lsys -lstdc++ -lpthread -liconv -lz"

# --- dtksh: build ksh93 before make reaches it ------------------------------
# programs/dtksh/Makefile.am builds ksh93 by invoking AST's own build system:
#
#     ksh93/bin/ksh:
#             ksh93/bin/package flat make CCFLAGS='$(KSH93_SHOPTS)'
#
# That target has no prerequisites, so if ksh93/bin/ksh already exists make
# leaves it alone.  This phase is what puts it there, having run the AST build
# with the cross harness it needs:
#
#   * an INIT cc.<hosttype> intercept, so every AST compile goes through the
#     substrate cross gcc (-std=gnu99: AST is K&R-era and gcc 16's C23 default
#     rejects its empty-paren declarations and implicit printf);
#   * mamake built and run NATIVELY — it drives the build, it is not part of
#     the product — and likewise lcgen, the one table generator outside iffe;
#   * iffe's probes executed on substrate through crossexec.
#
# Without this, `package` builds the whole of ksh93 with the host compiler and
# the link fails with "i386:x86-64 architecture ... is incompatible".
if [ -z "${DTKSH_ARG}" ]; then
    KSHROOT="programs/dtksh/ksh93"
    if [ ! -x "${KSHROOT}/bin/ksh" ]; then
        echo "==> dtksh: building ksh93 (iffe probes run on substrate)"
        # Per-HOSTTYPE compiler intercept.  AST compiles two different kinds
        # of program with $CC and this has to tell them apart: the product
        # (ksh93 and its libraries), which is cross-compiled for substrate,
        # and AST's own build machinery under src/cmd/INIT — mamake, proto,
        # probe, ratz — which package builds and then RUNS here.  Sending
        # those through the cross compiler is what produces
        # "mamake: Accessing a corrupted shared library" mid-build.
        #
        # The leading HOSTTYPE= line is a placeholder package sed-patches on
        # install; it also refuses to re-run when source and installed copies
        # are identical, so it has to be there.
        cat > "${KSHROOT}/src/cmd/INIT/cc.linux.i386" <<EOF
HOSTTYPE=
for _a in "\$@"; do
    case "\${_a##*/}" in
    mamake|mamake.c|mamake.o|proto|proto.c|proto.o|ratz|ratz.c|ratz.o|probe*)
        exec /usr/bin/cc "\$@" ;;
    esac
done
exec ${STAGE1_PREFIX}/bin/i386-unknown-substrate-gcc -std=gnu99 "\$@"
EOF
        chmod +x "${KSHROOT}/src/cmd/INIT/cc.linux.i386"

        # Drop any target binaries an earlier run left where the HOST has to
        # execute them.
        for _f in "${KSHROOT}"/bin/*; do
            [ -f "${_f}" ] || continue
            file "${_f}" 2>/dev/null | grep -q 'ELF 32-bit LSB executable, Intel i386' && rm -f "${_f}"
        done

        # Native mamake bootstrap.  `package make INIT` errors out late — CDE's
        # trimmed copy of the AST tree has no INIT package — but it builds
        # bin/mamake first, which is all this needs.
        _NATARCH=$(cd "${KSHROOT}" && bin/package host type 2>/dev/null || echo linux.i386-64)
        if [ ! -x "${KSHROOT}/arch/${_NATARCH}/bin/mamake" ]; then
            ( cd "${KSHROOT}" && PATH="${HOSTTOOLS}:/usr/bin:${PATH}" \
                bin/package make INIT >/dev/null 2>&1 || true )
        fi
        [ -x "${KSHROOT}/arch/${_NATARCH}/bin/mamake" ] || {
            echo "build.sh: dtksh: native mamake bootstrap failed" >&2; exit 1; }
        cp -f "${KSHROOT}/bin/package" "${KSHROOT}/arch/${_NATARCH}/bin/package"
        # package prepends $INSTALLROOT/bin to PATH and runs mamake from
        # there, so the native one has to be in the TARGET arch tree too.
        mkdir -p "${KSHROOT}/arch/linux.i386/bin"
        cp -f "${KSHROOT}/arch/${_NATARCH}/bin/mamake" \
              "${KSHROOT}/arch/linux.i386/bin/mamake"

        # lcgen is the one self-run generator outside iffe: pre-place a host
        # build so mamake finds it up to date and the ./lcgen run works.
        mkdir -p "${KSHROOT}/arch/linux.i386/src/lib/libast"
        cc -O -w -o "${KSHROOT}/arch/linux.i386/src/lib/libast/lcgen" \
            "${KSHROOT}/src/lib/libast/port/lcgen.c"

        _SHOPTS=$(make -C programs/dtksh -pn 2>/dev/null | \
                  grep -m1 '^KSH93_SHOPTS = ' | sed 's/^KSH93_SHOPTS = //')
        ( cd "${KSHROOT}" && \
          PATH="$PWD/arch/${_NATARCH}/bin:${HOSTTOOLS}:$PWD/bin:${STAGE1_PREFIX}/bin:/usr/bin:${PATH}" \
          SUBSTRATE_EXEC_IMG="${EXEC_IMG}" \
          IFFEFLAGS="-x linux.i386" \
          bin/package flat make HOSTTYPE=linux.i386 "CCFLAGS=${_SHOPTS}" )
    fi
fi

echo "==> make -j${JOBS}"
make -j"${JOBS}" "$@"

# --- stage -----------------------------------------------------------------
# -k: the install-exec-hook `chown root` steps cannot succeed in an unprivileged
# build.  The setuid bits are applied when the image is baked; without -k the
# first chown would stop the install and everything after it would go unstaged.
echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
make -k install DESTDIR="${DESTDIR}" "$@" || true

# CDE bakes the ksh it found at configure time into the `#!` line of every
# generated ksh script — dtsession_res, dtappintegrate, dtopen, the
# Xsession.d fragments, Xsetup, ...  That is the hosttools mksh, at a path
# that does not exist on the target, so the kernel's shebang handler fails
# with ENOENT and e.g. dtsession_res cannot xrdb the CDE resources at session
# start.  Point them at the target's own ksh, preserving any shebang
# arguments.
echo "==> rewriting build-host ksh shebangs -> /bin/ksh"
grep -rIl '^#!.*hosttools.*ksh' "${DESTDIR}" 2>/dev/null | while IFS= read -r f; do
    sed -i '1{/^#!.*hosttools/s|^#! *[^ ]*ksh|#!/bin/ksh|}' "$f"
done
echo "    $(grep -rIl '^#!/bin/ksh' "${DESTDIR}" 2>/dev/null | wc -l) scripts now use /bin/ksh"

echo "==> CDE staged in ${DESTDIR}"
