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
# ports use).
set(_substrate_warn "-Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=format -Wno-error=format-security")
set(CMAKE_C_FLAGS_INIT   "-march=i486 -mtune=i486 ${_substrate_warn}")
set(CMAKE_CXX_FLAGS_INIT "-march=i486 -mtune=i486 ${_substrate_warn}")

# Pull transitive DT_NEEDED libs (libtqt-mt -> Xft/X11/freetype/...; libstdc++
# -> libpthread) through the link, and point the linker at the sysroot for
# -rpath-link resolution.  (substrate's libstdc++.so now carries libpthread in
# its DT_NEEDED, so C++ links resolve pthread_* without an explicit -lpthread.)
# substrate's `gcc -shared` adds no implicit libc, so a shared library with
# no other dependency (e.g. tdelibs' libtdefakes) can't resolve plain libc
# symbols like errno -- and tdelibs links its shared objects with
# -Wl,--no-undefined, turning that into a hard error.  Always link libc.
set(_substrate_link "-L${SUBSTRATE_SYSROOT}/lib -Wl,-rpath-link,${SUBSTRATE_SYSROOT}/lib -Wl,--copy-dt-needed-entries -l:libc.so.0")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_substrate_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_substrate_link}")

set(CMAKE_FIND_ROOT_PATH "${SUBSTRATE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
