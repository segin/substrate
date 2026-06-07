# CDE (Common Desktop Environment) — substrate port (in progress)

CDE is the classic Motif-based Unix desktop (dtwm, dtsession, dtlogin,
dtfile, dtterm, dtpad, dtcm, dthelp, dtksh, ...), open-sourced as
[cdesktopenv](https://sourceforge.net/projects/cdesktopenv/).  This is a
**staged, in-progress port** — CDE is a very large system with several
prerequisite subsystems substrate does not yet have, so this directory
currently lays the foundation and tracks the remaining work.

## Source and build system

`fetch.sh` clones the **`C23-GCC15-Changes`** branch — substrate's toolchain
is GCC 16, and the 30-year-old CDE sources only compile cleanly on that
branch.  The modern cdesktopenv build is **autotools** (`autogen.sh` →
`configure` → `make`); imake is no longer used (0 Imakefiles remain), which
makes cross-compiling far more tractable than the historical imake build.

`fetch.sh` also applies the standard substrate autotools adjustments
(idempotent, no patch series): `config.sub` learns the `substrate*` OS, and
libtool's generated `configure` treats `substrate*` like `linux*` so
`--enable-shared` works.

## Status

`./configure` (cross, against a Motif + X11 + libXinerama sysroot) runs
through the large majority of its checks.  Resolved so far:

- **Motif 2.3.8** — already ported (`contrib/motif`, `libXm`/`libMrm`).
- **X client stack** — already ported (libX11/Xt/Xmu/Xaw/Xext/Xpm/ICE/SM).
- **libXinerama** — newly ported (`contrib/libXinerama/`) to clear the first
  hard `configure` error.
- **crypt** — substrate keeps `crypt()` in libc (no separate `libcrypt`);
  configure must not be told to add `-lcrypt`.

- **libjpeg** — ported (`contrib/libjpeg/`, IJG v9).
- **lmdb** — ported (`contrib/lmdb/`).  Surfaced `posix_memalign`, `O_SYNC`,
  `BYTE_ORDER` (libc), and `pthread_mutexattr_setpshared` (steered onto
  pthread mutexes, since substrate lacks the POSIX semaphores its BSD-flavoured
  default wanted).
- **mksh** — ported (`contrib/mksh/`) as the target `/bin/ksh`.
- **Tcl** — ported (`contrib/tcl/`, core 8.6).  Surfaced TIOCM_* bits,
  `IN6_ARE_ADDR_EQUAL`, `_SC_GET{PW,GR}_R_SIZE_MAX`, `pthread_attr_*scope`.

With these, **`configure` now passes every library check** (jpeg, lmdb, Tcl,
Motif, X) and stops at the host build-tool requirement below.

## Dependency roadmap (remaining, in rough priority order)

Host build tools — `configure`'s current stop point requires these on the
BUILD host: `rpcgen`, `ksh`/`ksh93`, `compress`, `sessreg`, `onsgmls`,
`mkfontdir`, `bdftopcf`:
1. **rpcgen** (`rpcsvc-proto`) — generates the ToolTalk RPC stubs at build time.
2. **ksh** (host) — build scripts; build mksh for the host, or install one.
3. **compress / sessreg / mkfontdir / bdftopcf / onsgmls** — ncompress, the
   X session/font tools, and an SGML parser (opensp), all host-side.

Target runtime subsystem — the critical path:
6. **Sun RPC / ToolTalk.**  ToolTalk (`lib/tt`) is CDE's IPC backbone
   (dtsession/dtfile/dtwm all message through it) and needs Sun RPC —
   `<rpc/rpc.h>`, XDR, `svc_register`, etc.  substrate's libc has no Sun RPC,
   so this means porting **libtirpc** (and wiring the headers) before
   ToolTalk, and therefore most of the desktop, can build.  This is the
   single biggest remaining piece.

Then CDE itself: the `libDt*` libraries, ToolTalk, **dtksh** (bundles ksh93),
and the `dt*` programs — each of which will surface further legacy-API gaps
(SVR4/streams, `nl_types` catgets, specific ioctls) in substrate's libc.

## Reproducing the configure so far

```sh
./fetch.sh
# merge Motif + X dist trees (incl. dist-libXinerama) into a sysroot, then:
cd build/cdesktopenv/cde
./configure --host=i386-unknown-substrate --prefix=/usr/dt \
    CC=i386-unknown-substrate-gcc CXX=i386-unknown-substrate-g++ \
    CPPFLAGS="-I<sysroot>/usr/include -I<sysroot>/usr/include/X11" \
    LDFLAGS="-L<sysroot>/usr/lib -Wl,-rpath-link,<sysroot>/usr/lib" \
    --with-tcl=/usr/lib
```

`build.sh` automates the sysroot assembly and this invocation; it stops at
the first unmet prerequisite above so progress is reproducible as each port
lands.
