# Tcl 8.6.16 — substrate port (core)

Core Tcl, a CDE dependency (`configure --with-tcl`; dtinfo and parts of the
build use it).

## Build

```sh
./fetch.sh
./build.sh        # -> dist-tcl/usr/{bin/tclsh,lib/libtcl8.6.a,include/tcl.h,...}
```

Requires a **host `tclsh`** (`tclsh`/`tclsh8.6` on the build host): Tcl's build
runs it as `NATIVE_TCLSH` to generate sources, while the target objects are
produced by the cross compiler.

## Substrate notes

- **Core only.**  `libtcl8.6.a` (PIC, so CDE's PIE binaries link it), `tclsh`,
  headers, the script library and `tclConfig.sh`.  The bundled optional
  packages (sqlite3, tdbc, thread, ...) are skipped — CDE needs only core
  Tcl, and sqlite3 wants `alloca()`, which substrate's libc doesn't expose.
- **Static.**  Built `--disable-shared`; `libtcl8.6.a` is compiled `-fPIC`
  so it links cleanly into PIE consumers.
- Building it surfaced and fixed several missing libc/header bits (TIOCM_*
  modem bits, `IN6_ARE_ADDR_EQUAL`, `_SC_GET{PW,GR}_R_SIZE_MAX`,
  `pthread_attr_*scope`) — see the libc commit, not worked around here.
