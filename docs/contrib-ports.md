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

**CDE** (`contrib/cde/`) is cross-built from the cdesktopenv tree,
pinned to an exact commit on the `C23-GCC15-Changes` branch (CDE is a
git repo, not a tarball; the pin replaces SHA verification).  Substrate
source changes are a `patches/` series applied by `fetch.sh` before
`autogen.sh`; only *generated* files (config.sub, configure, Makefiles)
are adjusted by seds.  `hosttools/build.sh` builds the build-host
programs CDE's configure/build need (rpcgen, mksh-as-ksh, compress,
sessreg, mkfontdir, bdftopcf, onsgmls, tradcpp); `build.sh` assembles a
Motif + X11 + libXinerama + libXScrnSaver + Tcl + libtirpc sysroot,
configures `-D__linux__`, host-builds the in-tree generator tools
(lineToData, mk_fonts_alias), and cross-builds.  Prerequisite ports:
**libXScrnSaver** (libXss), **libtirpc** (Sun RPC for ToolTalk),
**lmdb**, **libjpeg**, **Tcl**, **mksh**.

The CDE core desktop builds end-to-end — all libraries, ToolTalk, and
the programs dtwm, dtfile, dtsession, dtterm, dtpad, dtstyle, dtcalc,
dtmail, dtcm, dtprintinfo, dtsearchpath, dtspcd, dtscreen, dtsr,
dticon, dtcreate, dtlogin, ...  The full desktop **comes up live**:
dtsession starts ToolTalk (needs the kernel msg_name fix + the
`/etc/hosts` hostname→127.0.0.1 mapping), dtwm decorates clients and
**draws the Front Panel** (clock, calendar, file manager, mail,
workspace switch, trash, ...), and dtterm renders cleanly.

Three substrate fixes were needed beyond the build:

- the ld.so canonical-PLT fix (function-pointer equality — see Dynamic
  Linking Phase 4g; dtwm's front-panel widget class otherwise aborts
  with "Unresolved inheritance operation");
- the libc `MB_CUR_MAX` fix (it was hardcoded 4 while substrate is a
  single-byte locale, so dtterm took the `wchar_t`/`XwcDrawString` path
  and drew each ASCII cell as a glyph + 3 tofu boxes);
- `contrib/cde/install-localized-types.sh` (+ `cdemerge.py`, a
  `merge(1)` replica) which expands the `%|nls|` placeholders and
  installs the `/usr/dt/appconfig/types` Front Panel + datatype/action
  database that the skipped `localized`/`types` clusters never staged.

dtappbuilder (dtbuilder) + ttsnoop also build: their `*_ui.c/_ui.h`
are generated at build time by RUNNING dtcodegen, which links Motif —
`hosttools/build.sh` builds a native `dtcodegen-host` against the build
host's Motif (e.g. Arch `openmotif`) in a separate native CDE objdir
(`hosttools/cde-host`, `-static-libtool-libs` so it is relocatable),
and `build.sh` swaps it over the cross-built wrapper before src/ab and
ttsnoop run the generator.  Static-link fixups: `MRESOURCELIB=-lMrm`
(referenced by dtbuilder_LDADD, never set by configure), libABil's yacc
globals renamed (collide with Motif libUil.a's), ttsnoop's local
`_tt_sigset` renamed (collides with libtt.a's).  Without a host Motif
both programs are skipped.

dtinfo + dtdocbook build via host-native generators
(pmaker/dfiles/msgsets/mkdbd in hosttools prefix/cde-tools).  dtksh
builds through the AST package/mamake cross harness: an INIT
cc.linux.i386 intercept compiles with the cross gcc (-std=gnu99),
mamake runs natively, and iffe's output{}/run feature probes EXECUTE ON
SUBSTRATE via hosttools `crossexec` (boots a rootfs.img copy headlessly
in qemu, relays stdout/exit over @@IFFE@@-framed serial) — so ksh93's
FEATURE headers reflect real substrate behavior; needs a baked
rootfs.img (fresh checkouts: bake, re-run build.sh).

Still deferred: the tt_type_comp compilation of the ToolTalk type DB
(the `.ptype` sources themselves ARE staged, and `build.sh` runs the
pure-GENCPP `.dt`/`.fp` generation in programs/types so the full
DTTYPES set + dtwm.fp installs), the rest of `localized` (only the
C-locale types slice is staged), dthelp parser.
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
- **PsyMP3** (`contrib/psymp3/`, pinned to the `1.99.11-RELEASE`
  upstream tag with a vendored patch series) — a music player built on SDL2.  Its
  codec dependencies each ship as their own port: `libogg`
  (`contrib/libogg/`), `libvorbis` (`contrib/libvorbis/`), `libopus`
  (`contrib/libopus/`), `speex` (`contrib/speex/`), `faad2`
  (`contrib/faad2/`), `taglib` (`contrib/taglib/`) and `spandsp`
  (`contrib/spandsp/`).  Together these bring audio/multimedia playback
  to the userland.
- **mpg123** (`contrib/mpg123/`).
