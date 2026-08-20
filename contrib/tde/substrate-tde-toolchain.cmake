# CMake cross-compile toolchain for TDE on the substrate i386 target.
#
# Shared by every contrib/tde/* CMake sub-port (tqtinterface, tdelibs, ...).
# Setting CMAKE_SYSTEM_NAME flips CMake into cross-compile mode, so the
# configure-time compile/link checks build but are never executed.
#
# The cross sysroot ($SYSROOT, default /opt/substrate/i386-unknown-substrate)
# already carries the X11 client stack, freetype/fontconfig/Xft, libstdc++,
# and the rest of libtqt-mt's DT_NEEDED chain.  Libraries and headers are
# found ONLY in the sysroot (so the host's /usr X libs are never picked up);
# programs (moc/uic/pkg-config/perl/sh) are found on the build HOST, since
# the TDE build runs them at build time.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i386)

if(NOT DEFINED SUBSTRATE_SYSROOT)
  if(DEFINED ENV{SUBSTRATE_SYSROOT})
    set(SUBSTRATE_SYSROOT "$ENV{SUBSTRATE_SYSROOT}")
  else()
    set(SUBSTRATE_SYSROOT "/opt/substrate/i386-unknown-substrate")
  endif()
endif()

set(CMAKE_C_COMPILER   i386-unknown-substrate-gcc)
set(CMAKE_CXX_COMPILER i386-unknown-substrate-g++)
set(CMAKE_AR           i386-unknown-substrate-ar)
set(CMAKE_RANLIB       i386-unknown-substrate-ranlib)

# GCC 16 promotes several legacy-C/C++ warnings to hard errors by default;
# the TDE 14.x sources predate that.  Demote them (same set the ctwm/glib
# ports use).  -fcommon: TDE 14.x has file-scope globals defined (not
# extern-declared) in headers included by multiple TUs (e.g. libkonq's
# TextSortOrders); GCC 10+ defaults to -fno-common, turning those into
# multiple-definition link errors.  Restore the legacy tentative-merge.
set(_substrate_warn "-Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=format -Wno-error=format-security")
set(CMAKE_C_FLAGS_INIT   "-march=i486 -mtune=i486 -fcommon ${_substrate_warn}")
set(CMAKE_CXX_FLAGS_INIT "-march=i486 -mtune=i486 -fcommon ${_substrate_warn}")

# Pull transitive DT_NEEDED libs (libtqt-mt -> Xft/X11/freetype/...) through
# the link, and point the linker at the sysroot for -rpath-link resolution.
#
# On pthread: substrate's libstdc++.so.6 records NO DT_NEEDED entries at all
# (the libtool link that built it passed -nostdlib with no -l flags), so it
# does not pull libpthread in itself -- an earlier comment here claimed it
# did.  What actually satisfies libstdc++'s __gthread_* references is the
# toolchain `specs` override, which appends -lpthread to LIB_SPEC for every
# link; see contrib/gcc/install-specs.sh.  Nothing to do here, but do not
# "simplify" on the assumption that libstdc++ carries its own dependencies.
# substrate's `gcc -shared` adds no implicit libc, so a shared library with
# no other dependency (e.g. tdelibs' libtdefakes) can't resolve plain libc
# symbols like errno -- and tdelibs links its shared objects with
# -Wl,--no-undefined, turning that into a hard error.  Always link libc.
#
# substrate also splits the POSIX regex facility (regcomp/regexec/regfree)
# out of libc into libregex, where glibc keeps it in libc.  TDE therefore
# never asks for -lregex (kregexp.cpp, kjs, tdehtml all expect it in libc),
# so -- exactly as with the pthread split -- the toolchain supplies it.
# Linked plain (not --as-needed): CMake places these flags BEFORE the
# object files, where --as-needed would drop the library before the
# objects' undefined regex refs are seen.  Shared-object symbol resolution
# is not positional, so a plain -l: resolves regcomp regardless of order.
# The staged TDE sysroot (opt/trinity/lib) holds the cross-built TQt/TDE
# shared libs.  ld needs it on -rpath-link (not just -L) so it can resolve
# the *indirect* DT_NEEDED of a library named on the link line -- e.g.
# linking against libtdeio.so pulls in its NEEDED libtdesu.so.14, which ld
# only locates via -rpath-link.  Derived from this file's location
# (contrib/tde/) so it needs no extra configuration; harmless before the
# tree is populated (ld ignores a non-existent -rpath-link dir).
get_filename_component(_tde_top "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_tde_staged_lib "${_tde_top}/dist-overlay/dist-tde-sysroot/opt/trinity/lib")
# --allow-shlib-undefined: TDE's tdeinit_* executables link the wrapper
# libtdeinit_<app>.so by file, which itself NEEDs other build-tree .so
# (e.g. libtdeinit_kicker.so -> libkonq.so.4 in obj/libkonq).  Those
# build-tree dirs are not on -rpath-link, so ld can't follow the indirect
# NEEDED at link time -- but the DT_NEEDED is correct and ld.so resolves
# it at runtime.  Tell ld to trust the shared libs' own dependencies.
set(_substrate_link "-L${SUBSTRATE_SYSROOT}/lib -Wl,-rpath-link,${SUBSTRATE_SYSROOT}/lib -L${_tde_staged_lib} -Wl,-rpath-link,${_tde_staged_lib} -Wl,--copy-dt-needed-entries -Wl,--allow-shlib-undefined -l:libc.so.0 -l:libregex.so.0")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_substrate_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_substrate_link}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_substrate_link}")

set(CMAKE_FIND_ROOT_PATH "${SUBSTRATE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
