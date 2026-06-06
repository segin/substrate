# CMake cross-compile toolchain for the substrate i386 target.
#
# Setting CMAKE_SYSTEM_NAME flips CMake into cross-compile mode.  The X
# client libraries live in the merged mini-sysroot $ENV{X11ROOT}/usr; libs
# and headers are found ONLY there (so the host's /usr/lib X libs are never
# picked up), while programs (flex, bison, perl, m4, sh) are found on the
# build host (NEVER rooted) since ctwm runs them at build time.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i386)

set(CMAKE_C_COMPILER i386-unknown-substrate-gcc)
set(CMAKE_AR i386-unknown-substrate-ar)
set(CMAKE_RANLIB i386-unknown-substrate-ranlib)

# Link as a PIE so the libXt/libXmu WidgetClass globals resolve via
# R_386_GLOB_DAT instead of R_386_COPY (same as the twm/xterm ports), and
# pull transitive DT_NEEDED libs through the link.
# GCC 16 promotes several legacy-C warnings to hard errors by default; the
# ctwm sources predate that.  Demote them (same set the glib/pkg-config ports use).
set(CMAKE_C_FLAGS_INIT "-march=i486 -mtune=i486 -fPIE -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=format -Wno-error=format-security")
# -lregex: substrate's POSIX regcomp/regexec live in libregex, not libc; ctwm
# (USE_SREGEX) needs them.  -L points at the mini-sysroot where it is staged.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-pie -Wl,--copy-dt-needed-entries -L$ENV{X11ROOT}/usr/lib -Wl,-rpath-link,$ENV{X11ROOT}/usr/lib -lregex")

set(CMAKE_FIND_ROOT_PATH "$ENV{X11ROOT}/usr")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
