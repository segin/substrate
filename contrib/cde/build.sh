#!/bin/sh
#
# contrib/cde/build.sh — cross-configure (and, as prerequisites land, build)
# CDE for substrate.  Assembles a Motif + X11 + libXinerama sysroot and runs
# CDE's autotools configure.  Until the remaining prerequisite ports exist
# (libjpeg, Tcl, rpcgen, ksh, Sun RPC/ToolTalk — see README.SUBSTRATE.md),
# configure stops at the first unmet dependency; that is expected and the
# stop point advances as each port is added.
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

# Build-host programs CDE's configure requires (rpcgen, ksh, compress,
# sessreg, mkfontdir, bdftopcf, onsgmls).  hosttools/build.sh builds them
# from source into hosttools/prefix; prepend it (and the cross toolchain) to
# PATH so configure finds them.
HOSTTOOLS="${HERE}/hosttools/prefix/bin"
[ -x "${HOSTTOOLS}/rpcgen" ] || ( cd "${HERE}/hosttools" && ./build.sh )
PATH="${STAGE1_PREFIX}/bin:${HOSTTOOLS}:${PATH}"; export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Assemble the mini-sysroot: Motif + the X client stack + libXinerama +
# libjpeg + lmdb + Tcl, plus substrate's core libs (lmdb DT_NEEDEDs
# libpthread; configure link tests pull libc/libsys).
SR="${HERE}/build/sysroot"
rm -rf "${SR}"; mkdir -p "${SR}/usr/lib"
_have=0
for d in xorgproto libXau xtrans libxcb libX11 libXext libICE libSM \
         libXt libXmu libXpm libXaw libXinerama libXScrnSaver libjpeg lmdb tcl libtirpc motif; do
    st="${SUBSTRATE_TOP}/dist-${d}"
    [ -d "${st}/usr" ] || continue
    cp -a "${st}/usr/." "${SR}/usr/"
    _have=$((_have + 1))
done
[ "${_have}" -ge 19 ] || { echo "build.sh: only ${_have} dist trees found — build the X stack + Motif + libXinerama + libXScrnSaver + libjpeg + lmdb + tcl first" >&2; exit 1; }
for l in c sys m pthread; do
    cp "${SUBSTRATE_TOP}/lib/${l}/lib${l}.so.0" "${SR}/usr/lib/" 2>/dev/null || true
done

# Build IN-SOURCE.  CDE's Makefiles reference its exported Dt/* headers as
# -I../../include (relative to the build dir), so an out-of-source build can't
# find them — the headers live in the source tree.  Configure and make in
# TREE_DIR directly.
cd "${TREE_DIR}"

# Link-order fix: ttsession lists libtt before libstt, but libstt references
# libtt's API and both are static archives, so libtt must follow libstt.
# Append it (idempotent).
TTS="lib/tt/bin/ttsession/Makefile.am"
if [ -f "${TTS}" ] && ! grep -q 'lib/.libs/libtt.a' "${TTS}"; then
    sed -i 's@\(ttsession_LDADD = \$(LIBTT) \$(X_LIBS) \.\./\.\./slib/libstt\.a\)@\1 ../../../../lib/tt/lib/.libs/libtt.a@' "${TTS}"
fi

echo "==> configure"
# -D__linux__ -Dlinux: CDE has a Linux port and selects its modern code paths
# (vs old SVR4/SunOS) on these; substrate is pthread + ELF + glibc-like + BSD
# sockets, so the Linux paths are the right ones.  CDE guards on BOTH the
# modern __linux__ and the legacy `linux` predefine (e.g. `#ifndef linux`
# around the BSD-only SO_USELOOPBACK), and substrate's cross gcc defines
# neither, so define both.  Without them CDE pulls in legacy declarations that
# conflict with substrate's headers (its own extern ioctl, SO_USELOOPBACK, ...).
# crypt lives in substrate libc — do NOT let configure add -lcrypt.
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr/dt \
    CC=i386-unknown-substrate-gcc \
    CXX=i386-unknown-substrate-g++ \
    CPPFLAGS="-D__linux__ -Dlinux -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=return-mismatch -Wno-error=format -I${SR}/usr/include -I${SR}/usr/include/X11 -I${SR}/usr/include/tirpc" \
    LDFLAGS="-L${SR}/usr/lib -Wl,-rpath-link,${SR}/usr/lib" \
    --with-tcl="${SR}/usr/lib"

# Deferred programs — each blocks on a separate effort, not a substrate gap:
#   dtksh : ksh93's AST mamake/iffe detects the BUILD host
#           (HOSTTYPE=linux.i386-64) and compiles libast/libcmd/libshell for
#           x86-64 instead of the i386 cross target; the final link fails on
#           incompatible objects.  (Substrate side done: libc symbol surface +
#           libiconv plain names.)
#   dtappbuilder : builds its libs + dtcodegen fine (the Motif port now provides
#           libUil), but src/ab then RUNS the cross-built dtcodegen to generate
#           *_ui.h headers — and dtcodegen links Motif, so it can't execute on
#           the build host.  Host-building it needs a host Motif (all of libXm),
#           a separate large effort.  (dtcm only needs the uil/ public headers,
#           now installed by the Motif port, so it builds.)
#   ttsnoop : runs the same dtcodegen generator at build time, so it follows
#           dtappbuilder.
#   dtinfo,
#   dtdocbook : need their own SGML build tooling (pmaker/Prelude.h generator,
#           the dtdocbook parser) which is cross-built and won't run on the host.
#   tttypes,
#   types   : run the cross-built tt_type_comp (ToolTalk type compiler, which
#           links libtt) during the build — it can't execute on the host, and
#           host-building it needs the whole ToolTalk chain.
#   localized : its message-catalog generators (merge/mkcatdefs) cross-build vs
#           host-run conflict, and it only produces translated app-defaults,
#           not core function.
#   dthelp  : the helptag SGML parser (canon1/pass1/pass2) runs a swarm of
#           cross-built generators (context/fclndir/eltdef/merge/...).  The help
#           LIBRARY (lib/DtHelp) is already built; this is the document compiler.
# Drop them from the programs SUBDIRS (each token removed idempotently) so the
# rest of CDE builds to completion.  This builds the CDE core desktop (dtwm,
# dtfile, dtsession, dtterm, dtpad, dtstyle, dtcalc, dtmail, dthelp, dtprintinfo,
# dtsearchpath, dtspcd, dtscreen, dtsr, ...); the deferred set is the App
# Builder / ksh93 / dtinfo / ToolTalk-types / localized clusters, each tracked
# as a separate effort.
for _skip in dtksh dtappbuilder ttsnoop dtinfo dtdocbook tttypes types localized dthelp; do
    # Match the token followed by whitespace OR end-of-line (last SUBDIRS entry).
    sed -i "s/[[:space:]]${_skip}\([[:space:]]\|$\)/\1/" programs/Makefile
done

# The top-level `doc` subdir builds CDE's man pages by running dtdocbook2man,
# which is part of the deferred dtdocbook.  Drop it (man pages are not part of
# the desktop runtime).
sed -i 's/^am__append_1 = doc$/am__append_1 =/' Makefile

# In-tree generator tools: CDE compiles several small noinst_PROGRAMS and runs
# them mid-build to generate source (lineToData -> TermLineData.c, ...).  The
# cross-build compiles them for the target, so they can't execute on the build
# host (make fails with "Error 126").  Pre-build each with the host cc; the
# host object + binary are newer than their sources, so the subsequent cross
# make treats them as up-to-date and skips the target rebuild.  Their objects
# are noinst generators and never link into target artifacts, so a host object
# sitting in the tree is harmless.
# Each entry: "tool-dir:tool:generated-file".  The generated file is removed so
# it is regenerated by the now-working host tool: a generated source whose make
# rule lists only its *.data input (older than any stale output left by an
# earlier failed cross run) would otherwise never be rebuilt, and an empty
# generated file links to an object with no symbols.
HOST_GEN_TOOLS="
lib/DtTerm/util:lineToData:../Term/TermLineData.c
programs/fontaliases:mk_fonts_alias:
"
for entry in ${HOST_GEN_TOOLS}; do
    gdir=$(echo "${entry}" | cut -d: -f1)
    gtool=$(echo "${entry}" | cut -d: -f2)
    gout=$(echo "${entry}" | cut -d: -f3)
    echo "==> host-build ${gdir}/${gtool}"
    # Remove the tool and its objects first: a stale CROSS-built (target) binary
    # left newer than its sources by an earlier run would otherwise be "up to
    # date" and skip the host rebuild, leaving a binary that can't run here.
    rm -f "${gdir}/${gtool}" "${gdir}"/*.o
    make -C "${gdir}" CC=cc CPPFLAGS= CFLAGS='-O2 -w' "${gtool}"
    [ -n "${gout}" ] && rm -f "${gdir}/${gout}"
done

echo "==> make -j${JOBS}"
# GENCPP: CDE expands its *.cpp config templates with util/tradcpp, but the
# cross-build compiles that for the target (can't run on the build host).
# hosttools builds a host tradcpp; point every Makefile's GENCPP at it.  A
# command-line override propagates to all recursive sub-makes via MAKEFLAGS.
#
# LIBS: substrate splits the raw syscall wrappers (setsid, ...) into libsys,
# which the CDE static libs (libDtSvc) reference.  libc.so DT_NEEDEDs libsys,
# but ld won't resolve through a transitive DSO, so every executable must link
# -lsys directly.  -lstdc++ pulls the shared C++ runtime (operator new/delete,
# __gxx_personality_v0) that CDE's C++ objects in libDtSvc/libDtMail need — it
# is required even for the C-driver (gcc) links like dtpad that drag those
# objects out of the static archives.  configure sets LIBS uniformly to
# "-ldl -lm"; append -lsys -lstdc++.
make -j"${JOBS}" GENCPP="${HOSTTOOLS}/tradcpp" LIBS="-ldl -lm -lsys -lstdc++ -liconv"

# Stage the built desktop into dist-cde (CDE installs under /usr/dt).  -k keeps
# going past the install-exec-hook `chown root` steps that fail in a non-root
# build (the setuid bits are re-applied when the image is baked); without -k the
# first chown stops the install and the later programs never stage.
echo "==> install into ${SUBSTRATE_TOP}/dist-cde"
rm -rf "${SUBSTRATE_TOP}/dist-cde"
make -k install DESTDIR="${SUBSTRATE_TOP}/dist-cde" GENCPP="${HOSTTOOLS}/tradcpp" || true

# CDE's configure bakes the build-host ksh path (the hosttools mksh-as-ksh it
# found at build time) into the `#!` line of every generated ksh script —
# dtsession_res, dtappintegrate, dtopen, dtprintegrate, the Xsession.d/*
# fragments, Xsetup, ...  That path does not exist on the target, so the
# kernel's shebang handler fails with ENOENT ("exec: handler script failed for
# /usr/dt/bin/dtsession_res (-2)") and e.g. dtsession_res can't xrdb the CDE
# resources at session start.  Rewrite the interpreter to the target's
# /bin/ksh (mksh is installed there), preserving any shebang args.
echo "==> rewriting build-host ksh shebangs -> /bin/ksh"
grep -rIl '^#!.*hosttools.*ksh' "${SUBSTRATE_TOP}/dist-cde" 2>/dev/null | while IFS= read -r f; do
    sed -i '1{/^#!.*hosttools/s|^#! *[^ ]*ksh|#!/bin/ksh|}' "$f"
done
echo "  $(grep -rIl '^#!/bin/ksh' "${SUBSTRATE_TOP}/dist-cde" 2>/dev/null | wc -l) scripts now use /bin/ksh"

echo "==> CDE build complete (if you reached here, all prerequisites are in place)"
