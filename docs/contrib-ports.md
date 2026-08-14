# Userland Ports (contrib/)

Every third-party userland lives under `contrib/<pkg>/` as a patch
series against an upstream tarball — never vendored source.  Standard
layout per port:

- `fetch.sh` — download + SHA-verify + extract + apply patches
- `build.sh` — configure + make + stage into `dist-overlay/dist-<pkg>/usr/`
- `patches/` — the patch series
- `series` — patch manifest
- `README.SUBSTRATE.md` — port notes

`build-rootfs.sh` overlays every `dist-overlay/dist-*` tree onto the
image.  This document catalogs the current set.

## Core system / shell / text

- **GNU make 4.4.1** (`contrib/make/`)
- **GNU sed 4.9** (`contrib/sed/`)
- **OpenBSD expr** (`contrib/expr/`) — single-file BSD port alongside
  the OpenBSD `tr` port at `bin/tr/`.
- **zsh 5.9** (`contrib/zsh/`) — the bash-equivalent shell.  System
  `/bin/sh` is a symlink to `/usr/bin/zsh`; argv[0] detection puts zsh
  in POSIX `sh` emulation when invoked that way.  The in-tree `bin/sh/`
  is retained but disabled at the `bin/Makefile` SUBDIRS level — zsh
  covers everything autoconf needs.
- **ncurses 6.4** (`contrib/ncurses/`) — full terminfo backend.
  Replaces the link-time stub `lib/curses/` (kept in-tree but disabled
  at `lib/Makefile` SUBDIRS).  Brings tic / tput / clear / reset /
  tset / infocmp + the 2851-entry upstream terminfo database under
  `/usr/share/terminfo/`.  Substrate's hand-rolled `bin/clear` and
  `bin/reset` are retained as fallbacks for the no-ncurses embedded
  profile.

## Compression / archive / crypto / net

- **bzip2 1.0.8** (`contrib/bzip2/`)
- **gzip** (`contrib/gzip/`)
- **libarchive 3.7.7** + bsdtar (`contrib/libarchive/`)
- **OpenSSL 3.x** (`contrib/openssl/`)
- **curl** (`contrib/curl/`)
- **libiconv 1.17** (`contrib/libiconv/`)
- **zlib 1.3.1** (`contrib/zlib/`) — DEFLATE/gzip runtime, pulled in as
  a dependency of mandoc.
- **tzdata 2024a** (`contrib/tzdata/`)
- **inetutils** (`contrib/inetutils/`) — telnetd, ping, etc.

## Documentation / pager

- **mandoc 1.14.6** (`contrib/mandoc/`) — substrate's man-pager
  toolchain (`mandoc`, `man`, `makewhatis`, `apropos`, `whatis`).
  Cross-compile probe results are overridden via `configure.local`
  (HAVE_FTS, HAVE_REALLOCARRAY, HAVE_STRSEP, HAVE_STRCASESTR,
  HAVE_MKSTEMPS = 1; HAVE_WCHAR, HAVE_DIRENT_NAMLEN = 0).  Reads/writes
  the `mandoc.db` index at `/usr/share/man/mandoc.db`.
- **less 692** (`contrib/less/`) — system `$PAGER` (also wired as
  `more`).  Configured with `--with-regex=posix` against libregex;
  tinfo/pcre auto-detection is suppressed via `ac_cv_lib_*=no`.
- **qman 1.5.1** (`contrib/qman/`) — fetched but not yet buildable on
  substrate (needs meson, cog, libbsd, ncursesw).  Tracked under
  `contrib/qman/README.SUBSTRATE.md`.

## X11 client library stack

The six packages that build Xlib, in dependency order:

- **xorgproto 2024.1** (`contrib/xorgproto/`) — X protocol headers
  (`X.h`, `Xproto.h`, `keysymdef.h`, extensions).
- **xcb-proto 1.17.0** (`contrib/xcb-proto/`) — XCB protocol XML + the
  `xcbgen` Python generator (build-time only).
- **libXau 1.0.12** (`contrib/libXau/`) — X authority file
  (`~/.Xauthority`) library; `libXau.so.6`.
- **xtrans 1.6.0** (`contrib/xtrans/`) — X transport-layer `.c`/`.h`
  files compiled into libX11 (header-only port).
- **libxcb 1.17.0** (`contrib/libxcb/`) — X C Binding; `libxcb.so.1` +
  24 extension libraries.  A bundled `pkgconfig/pthread-stubs.pc`
  resolves the pthread-stubs dependency to substrate's real
  `-lpthread`.
- **libX11 1.8.12** (`contrib/libX11/`) — Xlib; `libX11.so.6` +
  `libX11-xcb.so.1`.  Built `--enable-xthreads` (1.8 nests
  non-threading code inside `#ifdef XTHREADS`); uses only pthread
  mutex/cond/self, no TLS keys.

All build shared + static.  Porting them added the POSIX
`IN6_IS_ADDR_*` macros to `<netinet/in.h>`, a `pthread_key_t` type to
`<pthread.h>`, and an `#ifndef bzero` guard in `<strings.h>`.

## X toolkit, terminal, and window managers

- **X toolkit + xterm** — `libXext` 1.3.7, `libICE` 1.1.2, `libSM`
  1.2.6, `libXt` 1.3.1, `libXmu` 1.3.1, `libXpm` 3.5.19, `libXaw`
  1.0.16 (Athena widgets), **`xterm` 410** (`contrib/xterm/`) and
  **`xauth` 1.1.5** (`contrib/xauth/`, the X authority /
  `MIT-MAGIC-COOKIE-1` tool).  xterm uses the core X bitmap fonts +
  Athena toolbar (Xft/freetype disabled).  No X server is ported —
  these are client-side; functional use needs an X server over TCP.
- **luit** (`contrib/luit/`) — Unicode/locale ISO-2022 filter that
  bridges a UTF-8 locale to a legacy-encoded child; xterm spawns it.
- **Window managers** — `matwm2` (`contrib/matwm2/`, the default
  session leader), **`twm` 1.0.12** (`contrib/twm/`, autotools) and
  **`ctwm` 4.1.0** (`contrib/ctwm/`, CMake; USE_JPEG/XRANDR/M4 off,
  HAS_REGEX pre-seeded against `libregex`, `lrand48`→`random` patch).
- **X bitmap fonts** — `font-misc-misc` 1.1.3 (the `fixed`/`9x15` misc
  family) and **`font-adobe-75dpi` / `font-adobe-100dpi`** 1.0.4
  (helvetica/times/courier/...).  Ports stage the `.bdf` sources
  verbatim (substrate has no `bdftopcf`; libXfont reads BDF directly)
  with a generated `fonts.dir`.  The adobe ports also DERIVE ISO8859-1
  single-byte variants from the ISO10646-1 masters: the X11
  `en_US.UTF-8` `XLC_FONTSET` binds its Latin slots
  (`ISO8859-1:GL`/`:GR`) to 1-byte fonts, and with only 2-byte
  ISO10646-1 fonts present libX11's `XmbDrawString` pairs bytes into
  bogus `XChar2b` indices → tofu boxes (the "twm font bug").  See the
  port READMEs.

## CDE (Common Desktop Environment)

**CDE** (`contrib/cde/`) is cross-built from the cdesktopenv git tree,
pinned to a commit on the `C23-GCC15-Changes` branch (substrate's GCC 16
defaults to C23, which rejects the empty-paren prototypes the 30-year-old
sources are full of; that branch is upstream's fix).  There is no release
tarball, so the commit is the reproducibility anchor instead of a SHA-256.

The hard part is not the compiler.  CDE builds roughly two dozen small
programs and then **runs them mid-build** to generate source, message
catalogs, ToolTalk type databases, font aliases and help volumes — none of
which can execute when cross-compiled.  The port answers that once rather
than case by case:

- `hosttools/build.sh` builds a complete **native** objdir of the same CDE
  tree at `hosttools/cde-host`, so every generator exists as a runnable
  host binary in the same relative location it occupies in the cross tree.
  It also builds the ordinary build-host dependencies from source (rpcgen,
  mksh-as-ksh, compress, sessreg, mkfontdir, bdftopcf, onsgmls, tradcpp).
- `build.sh` points CDE's own generator variables at that tree.  Because
  automake defines `subdir` in every Makefile, one set of command-line
  variables — `ELTDEF='$(CDE_HOST)/$(subdir)/eltdef'` and friends —
  redirects every generator in every directory.  Nothing is copied into
  the cross tree and nothing races make's timestamps.

Upstream already keeps most of these paths in variables (`GENCPP`,
`DTCODEGEN`, `TT_TYPE_COMP`, `MERGE`, `MKCATDEFS`, `MSGSETS`, `TREERES`);
the patch series does the same for the handful still hardcoded.  This is
why the port no longer skips the `types`, `localized` and `tttypes`
clusters, and why the Python `merge(1)` replica the previous port needed
(`cdemerge.py` + `install-localized-types.sh`) is gone.

The substrate patch series (`patches/`, applied **before** `autogen.sh`, so
the generated `configure` and Makefiles come out correct and `build.sh`
never seds them):

| patch | what |
|---|---|
| 0001 | `configure.ac`: select the OS from `host_os`, not `build_os` — only the former means anything in a cross build — and add a `substrate*` arm. |
| 0002 | ttsession: list libtt again after libstt (upstream relies on libtt being shared). |
| 0003 | libABil: private prefix for its yacc globals; they collide with Motif's libUil when both are static. |
| 0004 | ttsnoop: rename its private `_tt_sigset`, which libtt also defines. |
| 0005 | dtappbuilder: link `-lMrm` directly — `MRESOURCELIB` is referenced but never substituted. |
| 0006 | dtdocbook/instant: keep the Tcl paths as make variables so a cross build can aim them at its sysroot. |
| 0007 | Make the remaining in-tree build-time generators overridable. |
| 0008 | Do not hardcode the build host's `/usr/include/tirpc` into CFLAGS. |
| 0009 | `--disable-dtksh`, for hosts that cannot run target binaries. |

Two ordering hazards are worth knowing about, both recorded in comments at
the site:

- The `substrate*` arm in patch 0001 must come **before** `linux*`.  A
  substrate build post-processes the generated `configure` to teach
  libtool's `host_os` cases about the target, adding `substrate*` to every
  arm offering plain `linux*`; that pass cannot tell CDE's own OS case
  apart from libtool's, so the `linux*` arm inevitably absorbs substrate
  and has to lose the race.
- `LIBS` must carry `-lpthread` after `-lstdc++`: substrate's
  `libstdc++.so` has hard references to the pthread API but no DT_NEEDED
  on libpthread, so every C++-touching link otherwise fails on
  `pthread_mutex_init`.

Prerequisite ports: **motif** (libXm/libMrm/libUil), the X client stack
(libX11, libXt, libXext, libXmu, libXpm, libXaw, libICE, libSM,
**libXinerama**, **libXScrnSaver**), plus **libjpeg**, **lmdb**, **Tcl**,
**libtirpc** (Sun RPC, for ToolTalk) and **mksh** (the target `/bin/ksh`).

`--disable-docs` is passed: the `doc/` tree renders CDE's manual pages by
running the freshly built `dtdocbook` and `instant`, which are programs
rather than generators with an overridable path, so there is nothing to
redirect at the native objdir.

dtksh drives AST's own `package`/`mamake` build over the bundled ksh93 and
needs a compiler intercept that separates the product (cross-compiled) from
AST's build machinery — mamake, proto, probe, ratz — which must run on the
build host.  `hosttools/crossexec.d/crossexec` exists so iffe's run-type
probes can execute on substrate (headless qemu boot, results relayed over
an `@@IFFE@@`-framed serial protocol); it works standalone, but no qemu
boot was observed across a full ksh93 build, so those probe answers should
be treated as defaults.  See `contrib/cde/README.SUBSTRATE.md`.

Three substrate fixes CDE surfaced, all in the kernel and libraries rather
than here: the ld.so canonical-PLT fix (function-pointer equality — without
it dtwm aborts building the Front Panel with "Unresolved inheritance
operation"), the libc `MB_CUR_MAX` fix (hardcoded 4 on a single-byte
locale, so dtterm took the `XwcDrawString` path and drew every ASCII cell
as a glyph plus three tofu boxes), and `SO_PEERCRED` in the AF_UNIX
`getsockopt`.

The Motif port (`contrib/motif/`) builds libUil via Motif's WML
meta-compiler (host wml/wmluiltok) and installs the uil/ headers.

## Filesystem tooling

- **e2fsprogs 1.47.2** (`contrib/e2fsprogs/`) — mke2fs / e2fsck /
  tune2fs / debugfs / resize2fs / ... plus the static libext2fs /
  libcom_err / libe2p / libss / libuuid / libblkid.
- **e2tools 0.1.0** (`contrib/e2tools/`) — e2cp / e2ls / e2mkdir /
  e2rm / e2ln / e2mv / e2tail for manipulating unmounted ext2/3/4
  images.

## Debugger

- **gdb** (`contrib/gdb/`) — the GNU debugger, running natively on
  substrate atop the libsys `ptrace` PEEK bridge.  (See also
  `docs/toolchain.md`.)

## Build tooling

- **CMake 3.30.5** (`contrib/cmake/`) — provides two things:
  1. **Cross-compile support** for CMake-based projects — a reusable
     toolchain file (`substrate.toolchain.cmake`) plus a
     `Platform/Substrate` module set (`cmake-modules/`, which defers to
     CMake's Linux ELF conventions).  The host's cmake targets substrate
     via `-DCMAKE_TOOLCHAIN_FILE=...`.  It names the platform honestly
     (`CMAKE_SYSTEM_NAME=Substrate`), selects the `i386-unknown-substrate`
     toolchain + `i486` baseline, force-links `libc.so.0`/`libsys.so.0`
     into shared objects and `-lpthread` into every C++ link.
  2. **On-VM native cmake** (`fetch.sh` / `build.sh`) — cmake itself
     cross-built as a substrate ELF (cmake/ctest/cpack), staged into
     `dist-overlay/dist-cmake/`.  The bundled libuv uses its generic
     `poll(2)` backend (patch `0001`; substrate has no epoll/kqueue);
     patch `0002` drops source-specific multicast from libuv's udp.c.
     `build.sh` installs the `Platform/Substrate` modules into the staged
     module tree so native builds resolve the platform (a native cmake
     reports `uname -s == Substrate`).

## Multimedia / audio stack

- **SDL 2.30.9** (`contrib/sdl2/`) — with the X11 video driver and the
  NetBSD `/dev/audio` (Sun/SADA) audio backend.
- **FreeType2** (`contrib/freetype/`).
- **PsyMP3** (`contrib/psymp3/`, pinned to the `1.99.16-RELEASE`
  upstream tag with a vendored patch series) — a music player built on SDL2.  Its
  codec dependencies each ship as their own port: `libogg`
  (`contrib/libogg/`), `libvorbis` (`contrib/libvorbis/`), `libopus`
  (`contrib/libopus/`), `speex` (`contrib/speex/`), `faad2`
  (`contrib/faad2/`), `taglib` (`contrib/taglib/`) and `spandsp`
  (`contrib/spandsp/`).  Together these bring audio/multimedia playback
  to the userland.
- **mpg123** (`contrib/mpg123/`).
