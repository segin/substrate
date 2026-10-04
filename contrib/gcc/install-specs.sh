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
# Env: STAGE1_PREFIX (default /opt/substrate), GCC_VERSION (default 16.1.0),
#      TARGET_TRIPLE (default i386-unknown-substrate; x86_64-unknown-substrate
#      does the same for the 64-bit cross toolchain and its own sysroot).

set -eu

: "${STAGE1_PREFIX:=/opt/substrate}"
: "${GCC_VERSION:=16.1.0}"
: "${TARGET_TRIPLE:=i386-unknown-substrate}"

TARGET="${TARGET_TRIPLE}"
SR="${STAGE1_PREFIX}/${TARGET}"
LIBDIR="${STAGE1_PREFIX}/lib/gcc/${TARGET}/${GCC_VERSION}"

# GCC_VERSION's default tracks the pinned toolchain, but fall back to
# whatever single version is installed so a bump does not silently skip the
# specs -- the failure mode of a missing specs file is subtle (see above) and
# only shows up much later, in a C++ link or a throw that never gets caught.
if [ ! -d "${LIBDIR}" ]; then
    for _d in "${STAGE1_PREFIX}/lib/gcc/${TARGET}"/*/; do
        [ -d "$_d" ] || continue
        LIBDIR="${_d%/}"
    done
fi
[ -d "${LIBDIR}" ] || {
    echo "install-specs.sh: no such gcc libdir: ${LIBDIR}" >&2
    exit 1
}

# The *lib spec (LIB_SPEC) is the library list the driver expands *after* the
# user's objects, which is where -lpthread has to land to satisfy a static
# archive's references.
#
# libstdc++.a's exception-allocation pool (eh_alloc.o) calls
# __gthread_mutex_lock/unlock, i.e. pthread_mutex_lock/unlock.  Substrate keeps
# those in libpthread rather than libc -- glibc 2.34 merged them into libc
# precisely so this stops happening -- and libstdc++ records no dependency of
# its own, so every C++ link arrives at ld with them unresolved:
#
#   libstdc++.a(eh_alloc.o): in function `__gthread_mutex_lock(int*)':
#   gthr-default.h:795: undefined reference to `pthread_mutex_lock'
#
# Naming it here rather than per-project is what makes it stop recurring.  The
# GCC build alone hit it twice in two different subdirectories (isl's
# isl_test_cpp, then c++tools' g++-mapper-server), and c++tools has a
# hand-written link rule that uses neither LIBS nor LDFLAGS, so there is no
# configure-level knob that reaches every case.  Contrib ports, TDE and cmake
# have each needed the same flag bolted on separately.
#
# This is a workaround for the split, not a fix: moving the pthread symbols
# into libc would remove the need entirely.
cat > "${LIBDIR}/specs" <<EOF
*link:
+ %{!static:--eh-frame-hdr} --copy-dt-needed-entries -rpath-link ${SR}/lib

*lib:
+ -lpthread
EOF

# The 64-bit target also has to state its *libgcc spec.
#
# Having a specs file at all costs the driver more than the --eh-frame-hdr
# described above: it also skips the rewrite that turns the plain "-lgcc"
# into the static/shared choice (-lgcc -lgcc_eh, or -lgcc_s -lgcc), so every
# link gets libgcc.a alone -- with no unwinder, which lives in libgcc_eh.a
# and libgcc_s.so.1:
#
#   undefined reference to `_Unwind_Resume'
#   hidden symbol `__udivmodti4' in libgcc.a(_udivmoddi4.o) is referenced
#   by DSO
#
# (the second from libstdc++.so.6, which leaves its 128-bit division to
# whoever links it).  The 32-bit toolchain has the same hole and does not
# notice: its libm.so.0 records DT_NEEDED libgcc_s.so.1, libc.so.0 needs
# libm, and --copy-dt-needed-entries above therefore drags libgcc_s into
# every link.  The 64-bit libm.so.0 has no such dependency, so say it here:
# the shared libgcc first -- ahead of libgcc.a, so that a shared object's
# reference binds to it rather than pulling a hidden archive member -- and
# the static one only on request.  Every 64-bit program therefore needs
# /lib64/libgcc_s.so.1 at run time, as every 32-bit one in effect does.
case "${TARGET}" in
x86_64-*)
    cat >> "${LIBDIR}/specs" <<EOF

*libgcc:
%{static|static-libgcc:-lgcc -lgcc_eh;:-lgcc_s -lgcc}
EOF

    # ... and has to refuse the build host's library directories.
    #
    # -L/usr/lib64 names the TARGET's directory to whoever wrote it -- it is
    # the libdir of every 64-bit library -- but the linker runs on the build
    # host, where that path is the host's own library directory.  libtool
    # writes exactly this when it relinks a library at install time
    # (-L\$libdir).  The 32-bit toolchain survives it because the linker
    # skips what it finds there as the wrong format.  A 64-bit link cannot:
    # the host's libraries are x86-64 ELF too, and
    #
    #   ld: errno: TLS definition in /usr/lib64/libc.a(errno.o) section
    #   .tbss mismatches non-TLS reference in .../usr/lib64/libxcb.so
    #
    # is the lucky outcome -- the unlucky one links the host's libz.so and
    # says nothing.  Drop both directories from the link; the target's own
    # are already on the search path through the sysroot.
    cat >> "${LIBDIR}/specs" <<EOF

*link:
+ %<L/usr/lib64 %<L/usr/lib
EOF
    ;;
esac

echo "==> installed ${LIBDIR}/specs"

# Linker names for the substrate runtime libraries.
#
# A shared library has three names: the real file (libfoo.so.0), the soname
# recorded in DT_SONAME (what ld.so looks up at RUN time), and the bare
# `libfoo.so` symlink -- the LINKER name, which exists only so that -lfoo can
# find it.  Given -lfoo, ld tries exactly two filenames per search directory,
# `libfoo.so` then `libfoo.a`.  It never considers libfoo.so.0: the versioned
# name is invisible to -l.
#
# So a missing linker name does not produce an error, it silently redirects
# the link to the static archive.  That is the single most expensive trap in
# this tree; it has cost two multi-hour hunts already:
#
#   libstdc++  every shared object got a private C++ runtime -- its own
#              operator new/delete, iostream and locale globals, typeinfo.
#              The TDE desktop shipped that way across 125 libraries.
#   libdl      libdl.a is only five stubs forwarding to ld.so through a WEAK
#              reference to __ldso_dlopen.  A shared library keeps that
#              reference in .dynsym and ld.so binds it, so static libdl still
#              works there -- but an EXECUTABLE has it resolved to 0 at link
#              time and the symbol vanishes entirely, leaving a dlopen() that
#              returns NULL forever with dlerror() NULL too.  CMake hands
#              every project -ldl via CMAKE_DL_LIBS, which is how tdeinit
#              ended up unable to load a single TDE module.
#
# libc/libm/libpthread/libregex had the same gap.  It mattered least for them
# because substrate's own builds name the shared file explicitly
# (-l:libc.so.0), but the toolchain `specs` above appends -lpthread to every
# single link, so anything touching pthread symbols got a private copy of the
# threading runtime -- and two copies of pthread state in one process (an
# executable's and a .so's) is the same class of bug as the 125 libstdc++es.
#
# Assert every linker name here, where it is version controlled and runs after
# each toolchain (re)install.  Missing shared objects are reported, not
# silently skipped -- silence is what caused the problem in the first place.
for _lib in c m pthread regex dl sys; do
    _so="${SR}/lib/lib${_lib}.so"
    _real=""
    for _cand in "${SR}/lib/lib${_lib}.so".[0-9]*; do
        case "${_cand}" in *-gdb.py|*.py) continue ;; esac
        [ -f "${_cand}" ] || continue
        _real="${_cand}"
    done
    if [ -z "${_real}" ]; then
        echo "install-specs.sh: warning: no shared lib${_lib} in ${SR}/lib;" >&2
        echo "  -l${_lib} will link the static archive." >&2
        continue
    fi
    ln -sfn "$(basename "${_real}")" "${_so}"
    echo "==> linked ${_so} -> $(basename "${_real}")"
done

# The `libstdc++.so` link name.
#
# ld resolves -lstdc++ by looking for libstdc++.so first and libstdc++.a
# second, so with only the versioned libstdc++.so.6 / .so.6.0.35 present it
# silently falls through to the archive and links the C++ runtime *statically*
# -- with no diagnostic, because the link succeeds.
#
# That is not a size problem, it is a correctness one.  A stack built that way
# gives every DSO its own copy of the runtime: its own operator new/delete, its
# own iostream and locale globals, its own emergency exception pool.  The TDE
# desktop was staged for months as 125 shared libraries with 125 private
# libstdc++ copies for exactly this reason.
#
# build-libstdcxx-shared.sh creates this link when it installs the shared
# library, but it is a bare symlink in a directory nothing tracks, and it has
# gone missing at least once.  Re-assert it here, where it is version
# controlled and runs after every toolchain (re)install.
# Match only the real shared object: the directory also holds a
# libstdc++.so.6.0.35-gdb.py sidecar that a naive glob picks up.
SHARED_STDCXX=""
for cand in "${SR}/lib"/libstdc++.so.6.[0-9]*; do
    case "${cand}" in *-gdb.py) continue ;; esac
    [ -f "${cand}" ] || continue
    SHARED_STDCXX="${cand}"
done
if [ -n "${SHARED_STDCXX}" ]; then
    ln -sfn "$(basename "${SHARED_STDCXX}")" "${SR}/lib/libstdc++.so"
    echo "==> linked ${SR}/lib/libstdc++.so -> $(basename "${SHARED_STDCXX}")"
else
    echo "install-specs.sh: warning: no shared libstdc++ in ${SR}/lib;" >&2
    echo "  -lstdc++ will link the static archive.  Run" >&2
    echo "  contrib/gcc/build-libstdcxx-shared.sh to build it." >&2
fi
