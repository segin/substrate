# substrate.toolchain.cmake — reusable CMake cross-compile toolchain for the
# Substrate i386 target.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/contrib/cmake/substrate.toolchain.cmake ...
#
# Unlike the per-port toolchain files (contrib/ctwm, contrib/tde) that set
# CMAKE_SYSTEM_NAME=Linux, this one names the platform honestly as "Substrate"
# and ships a Platform/Substrate module set (cmake-modules/, which defers to
# CMake's Linux ELF conventions).  A native cmake running on the VM reports
# `uname -s == Substrate`, so cross and native builds resolve the same
# platform semantics.
#
# Tunables (cache var or environment):
#   SUBSTRATE_TOOLCHAIN_PREFIX  stage-1 cross prefix   (default /opt/substrate)
#   SUBSTRATE_SYSROOT           cross sysroot          (default <prefix>/i386-unknown-substrate)

set(CMAKE_SYSTEM_NAME Substrate)
set(CMAKE_SYSTEM_PROCESSOR i386)

# Teach this cmake where Platform/Substrate lives (next to this file).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake-modules")

# --- resolve the toolchain prefix + sysroot -------------------------------
if(NOT DEFINED SUBSTRATE_TOOLCHAIN_PREFIX)
  if(DEFINED ENV{SUBSTRATE_TOOLCHAIN_PREFIX})
    set(SUBSTRATE_TOOLCHAIN_PREFIX "$ENV{SUBSTRATE_TOOLCHAIN_PREFIX}")
  else()
    set(SUBSTRATE_TOOLCHAIN_PREFIX "/opt/substrate")
  endif()
endif()
if(NOT DEFINED SUBSTRATE_SYSROOT)
  if(DEFINED ENV{SUBSTRATE_SYSROOT})
    set(SUBSTRATE_SYSROOT "$ENV{SUBSTRATE_SYSROOT}")
  else()
    set(SUBSTRATE_SYSROOT "${SUBSTRATE_TOOLCHAIN_PREFIX}/i386-unknown-substrate")
  endif()
endif()

# --- compilers + binutils --------------------------------------------------
set(_sbin "${SUBSTRATE_TOOLCHAIN_PREFIX}/bin")
set(CMAKE_C_COMPILER   "${_sbin}/i386-unknown-substrate-gcc")
set(CMAKE_CXX_COMPILER "${_sbin}/i386-unknown-substrate-g++")
set(CMAKE_AR           "${_sbin}/i386-unknown-substrate-ar")
set(CMAKE_RANLIB       "${_sbin}/i386-unknown-substrate-ranlib")
set(CMAKE_STRIP        "${_sbin}/i386-unknown-substrate-strip")
# Fall back to the PATH-resolved names if the prefix layout differs.
foreach(_v C_COMPILER CXX_COMPILER AR RANLIB STRIP)
  if(NOT EXISTS "${CMAKE_${_v}}")
    string(TOLOWER "${_v}" _lc)
    string(REPLACE "_compiler" "" _lc "${_lc}")
    set(CMAKE_${_v} "i386-unknown-substrate-${_lc}")
  endif()
endforeach()

# --- default target flags --------------------------------------------------
# i486 baseline (the stage-2 GCC still defaults to pentium-pro); PIC code.
set(CMAKE_C_FLAGS_INIT   "-march=i486 -mtune=i486")
set(CMAKE_CXX_FLAGS_INIT "-march=i486 -mtune=i486")

# libsys owns the raw syscall() dispatcher and the sys_* typed wrappers
# (setsid, and everything the personality layer routes); every substrate
# binary links it, and the cross gcc/g++ driver does NOT add it implicitly, so
# name it explicitly for executables, shared libraries and modules.
# `gcc -shared` also adds no implicit libc, so shared objects/modules link libc
# too (executables get libc from the driver).  Point ld at the sysroot for
# -rpath-link resolution of indirect DT_NEEDED.
set(_substrate_ldpath "-L${SUBSTRATE_SYSROOT}/lib -Wl,-rpath-link,${SUBSTRATE_SYSROOT}/lib")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_substrate_ldpath} -l:libc.so.0 -l:libsys.so.0")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_substrate_ldpath} -l:libc.so.0 -l:libsys.so.0")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_substrate_ldpath} -l:libsys.so.0")

# --- find-root: libs/headers/packages ONLY in the sysroot, programs on host -
set(CMAKE_FIND_ROOT_PATH "${SUBSTRATE_SYSROOT}")
set(CMAKE_SYSROOT "${SUBSTRATE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
