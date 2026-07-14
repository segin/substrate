# CMake on Substrate

This port provides two things:

1. **Cross-compile support** — a reusable CMake toolchain file plus a
   `Platform/Substrate` module set, so any CMake-based project can be
   cross-built for the Substrate i386 target using the **host's** cmake.
2. **On-VM native cmake** *(see `build.sh`)* — cmake itself cross-built as a
   Substrate ELF, so cmake can run on the VM to configure/build packages
   there.  It installs the same `Platform/Substrate` modules so native
   builds work out of the box (a native cmake reports `uname -s == Substrate`
   and needs them).

## 1. Cross-compiling (host cmake → Substrate)

No cmake build is required — the host's cmake targets Substrate via the
toolchain file:

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=$PWD/contrib/cmake/substrate.toolchain.cmake \
      -S <srcdir> -B <builddir>
cmake --build <builddir>
```

Tunables (cache var or environment):

| var                          | default                              |
|------------------------------|--------------------------------------|
| `SUBSTRATE_TOOLCHAIN_PREFIX` | `/opt/substrate`                     |
| `SUBSTRATE_SYSROOT`          | `<prefix>/i386-unknown-substrate`    |

The toolchain:

* names the platform honestly as `Substrate` (`CMAKE_SYSTEM_NAME=Substrate`)
  and points `CMAKE_MODULE_PATH` at `cmake-modules/`, where the
  `Platform/Substrate*` modules defer to CMake's proven Linux ELF conventions
  (Substrate is an ELF/GNU/versioned-`.so` Unix);
* selects `i386-unknown-substrate-{gcc,g++,ar,ranlib,strip}` and the `i486`
  baseline;
* finds libraries/headers/packages **only** in the sysroot, and programs on
  the build **host** (they run at build time);
* force-links `libc.so.0` into shared libraries/modules (substrate's
  `gcc -shared` adds no implicit libc) and `-lpthread` into every C++ link
  (libstdc++.so has hard gthr-posix refs but no libpthread in its DT_NEEDED).

Verified for C and C++, static and shared libraries; the emitted binaries are
substrate ELF (`ET_EXEC`/`ET_DYN`, `PT_INTERP=/sbin/ld.so`, i386).

### OSABI branding

The cross g++/gcc stamp `ELFOSABI_SYSV(0)`.  Plain executables still run
(substrate accepts OSABI-0 exes), but shared objects must be branded
`ELFOSABI_SUBSTRATE(0x40)` for ld.so's personality dispatch.  Brand the
install tree at package time (as the other contrib ports do):

```sh
./contrib/cmake/substrate-osabi-stamp.sh "${DESTDIR}"
```

## 2. On-VM native cmake

See `fetch.sh` / `build.sh`.  `build.sh` cross-builds cmake as a Substrate
binary and stages it into `dist-overlay/dist-cmake/`.  See the build script
header for the dependency/libuv situation.

## Files

| file                                     | purpose |
|------------------------------------------|---------|
| `substrate.toolchain.cmake`              | reusable cross toolchain file |
| `cmake-modules/Platform/Substrate*.cmake`| platform modules (cross + native) |
| `substrate-osabi-stamp.sh`               | brand ELF output `0x40` at package time |
| `fetch.sh` / `build.sh`                  | fetch + cross-build native cmake |
