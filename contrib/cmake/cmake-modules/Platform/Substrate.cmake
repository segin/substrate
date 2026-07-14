# Platform/Substrate.cmake — CMake platform definition for Substrate.
#
# Substrate is an ELF, GNU-toolchain, versioned-shared-object Unix, so its
# link/rpath/soname conventions are those CMake already implements for Linux.
# Rather than duplicate them, defer to the stock Linux platform module; only
# the *name* differs (uname -s == "Substrate", so a native cmake on the VM
# and a cross toolchain both resolve Platform/Substrate here).
include(Platform/Linux)
