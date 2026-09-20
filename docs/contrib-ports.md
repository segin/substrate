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
  Built wide-character (`libncursesw`, `NCURSES_WIDECHAR 1`), with
  `libtinfo` kept under its plain name.  `-lncurses`, `-lcurses`,
  `-lform`, `-lmenu` and `-lpanel` are one-line linker scripts that
  forward to the wide libraries, so existing ports link unchanged.
- **nano 9.2** (`contrib/nano/`) — GNU nano, the small terminal editor.
  Needs ncurses, zlib and libmagic (`contrib/file`).  Carries no patches:
  building it surfaced four substrate-side gaps that were fixed there
  instead -- a self-contained `<wchar.h>`, `__fseterr()` in libc,
  `st_atim`/`st_mtim` in `struct stat`, and ncurses's `bool` definition.
  Built against the wide-character ncurses, so UTF-8 editing is enabled.
- **mc 4.8.33** (`contrib/mc/`) — GNU Midnight Commander, the two-panel
  file manager, with its viewer, `mcedit` and diff viewer.  Needs glib2,
  the wide-character ncurses and e2fsprogs.  No patches; it needed the wide
  ncurses and POSIX-typed `struct stat` fields on the substrate side.
  `build.sh` rewrites the `/usr` include paths from the staged `.pc` files
  into the sysroot, and links `-ldl` for gmodule.
- **perl 5.44.0** (`contrib/perl/`) — cross-built with perl-cross 1.6.5,
  unpacked over the perl tree.  A `substrate.hints` file (selected by
  `-Dosname=substrate`; perl-cross's `--hints=` is broken) supplies what
  cannot be probed, such as `d_nanosleep` and the 64-bit `st_ino_size`.
  One patch makes `Errno_pm.PL` read the target's `errno.h` rather than
  the build host's.
- **Python 3.14.7** (`contrib/python/`) — CPython, cross-built in two
  stages: a build-host interpreter from the same tarball (a cross build
  needs one of the same major.minor to freeze modules and compile the
  stdlib), then the substrate build via `--with-build-python`.  Links
  libffi, zlib, bzip2, openssl, ncurses, sqlite3, expat and uuid; 57
  extension modules including `_ssl`, `_sqlite3` and `_curses`.  One patch
  teaches configure that substrate takes the Linux path -- so
  `sys.platform` is `linux`, while `uname(2)` still says substrate -- and
  `--disable-ipv6` matches the kernel's refusal of `AF_INET6`.  `_dbm`,
  `_gdbm`, `_lzma`, `_zstd`, `_tkinter` and `readline` are not built: no
  port provides them.
- **texinfo 7.3** (`contrib/texinfo/`) — `info`, `install-info` and the
  `texi2any`/`makeinfo` translator.  The last is perl, so this follows
  `contrib/perl`; built `--disable-perl-xs`, using the pure-perl code path.

## Compression / archive / crypto / net

- **bzip2 1.0.8** (`contrib/bzip2/`)
- **gzip** (`contrib/gzip/`)
- **libarchive 3.7.7** + bsdtar (`contrib/libarchive/`)
- **OpenSSL 3.x** (`contrib/openssl/`) — configured `--openssldir=/etc/ssl`,
  so its defaults are `/etc/ssl/cert.pem` (CAfile) and `/etc/ssl/certs`
  (CApath).
- **ca-certificates** (`contrib/ca-certificates/`) — the system CA trust
  store: the Mozilla root bundle (via curl.se's NSS extract, pinned to a
  dated file + SHA256) installed at both of those paths, plus per-root
  subject-hash entries for CApath lookups, the `ca-certificates.crt`
  Debian-compatibility path, and an `update-ca-certificates` for adding
  local roots.  Nothing is compiled — the store is the deliverable.
  Before this, `/etc/ssl/certs` was empty and every TLS verification
  failed.
- **curl** (`contrib/curl/`) — built `--with-ca-bundle`/`--with-ca-path`
  pointing at the store above.  curl's configure otherwise probes the
  *build host* and bakes in whatever it finds, which was nothing, leaving
  `https://` broken even once the store existed.
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

Six more X extension libraries were ported to satisfy the applications
in "X.Org applications" below, all shared + static and OSABI-branded like
the rest:

- **libXrandr 1.5.5** (`contrib/libXrandr/`) — Client library for the RandR extension.
- **libXv 1.0.13** (`contrib/libXv/`) — Client library for the Xv video extension.
- **libXcomposite 0.4.7** (`contrib/libXcomposite/`) — Client library for the Composite extension.
- **libXdamage 1.1.7** (`contrib/libXdamage/`) — Client library for the Damage extension.
- **libXxf86vm 1.1.7** (`contrib/libXxf86vm/`) — Client library for the XFree86-VidModeExtension.
- **libFS 1.0.10** (`contrib/libFS/`) — Client library for the X Font Server protocol.
Alongside them, one non-X.Org library:

- **xcb-util 0.4.1** (`contrib/xcb-util/`) — client-side utility functions
  for XCB: `xcb-atom`, `xcb-aux` and `xcb-event`.  It comes from
  `xcb.freedesktop.org` rather than x.org; `xbacklight` needs its
  `xcb-atom` and `xcb-aux` pkg-config modules.

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

## X.Org applications

The rest of the X.Org `app/` collection, ported from the upstream
individual tarballs.  Each is a stock autotools cross build staged into
`dist-overlay/dist-<name>/usr`; none needs a patch series, because the only
substrate-specific fixup is `config.sub` (applied by
`substrate_config_sub_fix`) and libtool's host case (applied by
`substrate_libtool_fix`, without which libtool quietly builds the static
archive only).

### Clocks, logos and demos

- **xclock 1.2.1** (`contrib/xclock/`) — The classic analogue / digital clock.
- **oclock 1.0.6** (`contrib/oclock/`) — Round analogue clock; uses the SHAPE extension for its outline.
- **xlogo 1.0.7** (`contrib/xlogo/`) — The X logo.
- **ico 1.0.7** (`contrib/ico/`) — Animated polyhedron demo.
- **xgc 1.0.7** (`contrib/xgc/`) — Demo / test for the core drawing requests.
- **beforelight 1.0.6** (`contrib/beforelight/`) — Sample screen saver client driven by the MIT-SCREEN-SAVER extension.
- **x11perf 1.6.1** (`contrib/x11perf/`) — X server performance benchmark, with x11perfcomp to compare runs.
- **rendercheck 1.5** (`contrib/rendercheck/`) — Test suite for the RENDER extension.

### Desktop accessories

- **xload 1.2.2** (`contrib/xload/`) — System load average graph.
- **xbiff 1.0.6** (`contrib/xbiff/`) — Mailbox flag that raises when new mail arrives.
- **xconsole 1.1.1** (`contrib/xconsole/`) — Display console messages in a window.
- **xmessage 1.0.7** (`contrib/xmessage/`) — Display a message or ask a question in a window.
- **xclipboard 1.1.6** (`contrib/xclipboard/`) — Hold and browse CLIPBOARD selections.
- **xmag 1.0.8** (`contrib/xmag/`) — Magnify part of the screen.
- **xmore 1.0.4** (`contrib/xmore/`) — Plain-text pager in a window.
- **xditview 1.0.7** (`contrib/xditview/`) — View ditroff output.
- **bitmap 1.1.2** (`contrib/bitmap/`) — Bitmap editor, plus the bmtoa/atobm converters.

### Fonts

- **xfd 1.1.6** (`contrib/xfd/`) — Display every glyph in a font.
- **xfontsel 1.1.2** (`contrib/xfontsel/`) — Point-and-click font selector.
- **bdftopcf 1.1** (`contrib/bdftopcf/`) — Convert BDF bitmap fonts to PCF, the format the X server loads.
- **fonttosfnt 1.2.5** (`contrib/fonttosfnt/`) — Wrap a bitmap font in an sfnt (TrueType) container.
- **mkfontscale 1.2.4** (`contrib/mkfontscale/`) — Build the fonts.scale / fonts.dir index of a font directory.
- **fslsfonts 1.0.7** (`contrib/fslsfonts/`) — List the fonts served by an X font server.  Needs libFS.
- **fstobdf 1.0.8** (`contrib/fstobdf/`) — Read a font from an X font server and dump it as BDF.  Needs libFS.
- **showfont 1.0.7** (`contrib/showfont/`) — Dump a font from an X font server.  Needs libFS.
- **rgb 1.1.1** (`contrib/rgb/`) — The X colour-name database, plus showrgb.

### Display, window and event inspection

- **xdpyinfo 1.4.0** (`contrib/xdpyinfo/`) — Print display, screen and extension information.
- **xev 1.2.7** (`contrib/xev/`) — Print X events as they arrive.
- **xwininfo 1.1.7** (`contrib/xwininfo/`) — Print information about a window.
- **xlsatoms 1.1.5** (`contrib/xlsatoms/`) — List the atoms interned on a display.
- **xlsclients 1.1.6** (`contrib/xlsclients/`) — List the clients connected to a display.
- **xlsfonts 1.0.9** (`contrib/xlsfonts/`) — List the fonts a server knows about.
- **xwd 1.0.10** (`contrib/xwd/`) — Dump an X window to a file.
- **xwud 1.0.8** (`contrib/xwud/`) — Display an xwd dump.
- **xpr 1.2.1** (`contrib/xpr/`) — Convert an xwd dump to PostScript, PCL or HP-GL.
- **xrefresh 1.1.1** (`contrib/xrefresh/`) — Repaint all or part of the screen.

### Input, keyboard and pointer

- **xmodmap 1.0.12** (`contrib/xmodmap/`) — Edit the keyboard and pointer modifier maps.
- **setxkbmap 1.3.5** (`contrib/setxkbmap/`) — Set the keyboard map through the XKB extension.
- **xkbevd 1.1.6** (`contrib/xkbevd/`) — XKB event daemon.
- **xkbprint 1.0.8** (`contrib/xkbprint/`) — Print an XKB keyboard description.
- **xkbutils 1.0.7** (`contrib/xkbutils/`) — Small XKB demos: xkbbell, xkbvleds and xkbwatch.
- **xsetmode 1.0.0** (`contrib/xsetmode/`) — Set the mode of an XInput device.
- **xsetpointer 1.0.1** (`contrib/xsetpointer/`) — Choose which XInput device drives the core pointer.

### Screen, colour and video

- **xrandr 1.5.4** (`contrib/xrandr/`) — Query and change screen size, orientation and outputs.  Needs libXrandr.
- **xbacklight 1.2.4** (`contrib/xbacklight/`) — Adjust backlight brightness through RandR.
- **xgamma 1.0.8** (`contrib/xgamma/`) — Query and set monitor gamma through XF86VidMode.
- **xcmsdb 1.0.7** (`contrib/xcmsdb/`) — Load, query and remove Device Colour Characterization data.
- **xstdcmap 1.0.6** (`contrib/xstdcmap/`) — Define the standard colormap properties.
- **xvinfo 1.1.6** (`contrib/xvinfo/`) — Print Xv adaptor information.  Needs libXv.
- **xcompmgr 1.1.10** (`contrib/xcompmgr/`) — Sample compositing manager (Composite + Damage + Render).
- **transset 1.0.4** (`contrib/transset/`) — Set window transparency via _NET_WM_WINDOW_OPACITY.
- **xcursorgen 1.0.9** (`contrib/xcursorgen/`) — Build an Xcursor file from PNG images.

### Resources and session

- **appres 1.0.7** (`contrib/appres/`) — List the X resource database entries an application would see.
- **listres 1.0.7** (`contrib/listres/`) — List the resources of Xt widget classes.
- **viewres 1.0.8** (`contrib/viewres/`) — Browse the Xt widget class tree.
- **editres 1.1.1** (`contrib/editres/`) — Dynamic resource editor for Xt applications.
- **xhost 1.0.10** (`contrib/xhost/`) — Manage the host-based access control list.
- **sessreg 1.1.4** (`contrib/sessreg/`) — Add and remove utmp/wtmp entries for X sessions.
- **xsm 1.0.6** (`contrib/xsm/`) — X session manager.
- **smproxy 1.0.8** (`contrib/smproxy/`) — Session-management proxy for clients that do not speak XSMP themselves.
- **constype 1.0.6** (`contrib/constype/`) — Print the Sun console type.  SunOS-specific; kept for completeness.
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

## Multiple-precision arithmetic

- **GMP 6.3.0** (`contrib/gmp/`) — arbitrary-precision integers.  Built
  `--disable-assembly`: GMP selects its hand-written x86 asm path from
  the host triplet, and the portable C path is correct everywhere.
- **MPFR 4.2.2** (`contrib/mpfr/`) — correctly-rounded multiple-precision
  floats, on top of GMP.

  Both are build-order prerequisites of gdb, whose configure hard-fails
  with "Building GDB requires GMP 4.2+, and MPFR 3.1.0+" without them, so
  `build.sh`'s DEFAULT_CONTRIB lists them immediately before it.

## Debugger

- **gdb** (`contrib/gdb/`) — the GNU debugger, running natively on
  substrate atop the libsys `ptrace` PEEK bridge.  Requires GMP and MPFR
  in the cross sysroot (above).  (See also `docs/toolchain.md`.)

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

- **SDL stack** — three layered ports.  SDL3 is the only one that talks to
  the system; the older APIs are compatibility shims over it, so there is a
  single video/audio implementation to maintain:

      app -> libSDL-1.2.so.0 -> libSDL2-2.0.so.0 -> libSDL3.so.0 -> X11 / audio

  - **SDL 3.4.16** (`contrib/sdl3/`) — the base.  X11 video driver (dlopens
    `libX11.so.6`) and the NetBSD `/dev/audio` (Sun/SADA) audio backend;
    substrate is a first-class CMake platform (`CMAKE_SYSTEM_NAME=Substrate`)
    rather than being built as Linux.  Build it first.
  - **sdl2-compat 2.32.72** (`contrib/sdl2-compat/`) — provides the SDL2 ABI
    (`libSDL2-2.0.so.0`, `sdl2.pc`) on top of SDL3.  **Replaces the former
    `contrib/sdl2` port**; SDL2 consumers such as PsyMP3 link it unchanged.
    It dlopens `libSDL3.so.0` at run time.
  - **sdl12-compat 1.2.76** (`contrib/sdl12-compat/`) — provides the SDL 1.2
    ABI (`libSDL-1.2.so.0`) on top of SDL2, so 1.2-era software builds and
    runs without anyone maintaining a real SDL 1.2.  It dlopens
    `libSDL2-2.0.so.0` at run time.

  Because each layer dlopens the one below it rather than carrying a
  `DT_NEEDED`, all the layers an application needs must be installed on
  target, not just the one it links.
- **FreeType2** (`contrib/freetype/`, second pass `contrib/freetype-harfbuzz/`).
  FreeType and HarfBuzz depend on each other, so FreeType is built twice:
  first `--without-harfbuzz`, early in the list, then again right after
  harfbuzz with `FREETYPE_WITH_HARFBUZZ=1`, replacing `dist-freetype`.  The
  image's `libfreetype.so.6` links `libharfbuzz.so.0`, which links it back;
  ld.so resolves the cycle to one copy of each.  Ports built between the two
  passes link the first build, which has the same soname and symbols.
- **PsyMP3** (`contrib/psymp3/`, pinned to the `2.0-RC4` upstream tag
  with a vendored patch series) — a music player built on SDL3.  Its
  codec dependencies each ship as their own port: `libogg`
  (`contrib/libogg/`), `libvorbis` (`contrib/libvorbis/`), `libopus`
  (`contrib/libopus/`), `speex` (`contrib/speex/`), `fdk-aac`
  (`contrib/fdk-aac/`), `taglib` (`contrib/taglib/`) and `harfbuzz`
  (`contrib/harfbuzz/`).  Together these bring audio/multimedia playback
  to the userland.

  2.0-RC3 changed three dependencies from 1.99.16 — SDL2 to SDL3,
  `faad2` to `fdk-aac`, and `vorbis` to `vorbisenc vorbis` — and moved
  to subdirectory-qualified includes (`<SDL3/SDL.h>`, `<taglib/…>`,
  `<opus/…>`, `<fdk-aac/…>`), which resolve from the
  sysroot with no `-I` at all.  `<ft2build.h>` is the only bare name
  left, so `build.sh` names just `freetype2` explicitly; see the
  subdirectory-`Cflags` note under "Cross-build traps" for why that
  matters.  It also vendors `third_party/stb/stb_vorbis.c`, whose
  `alloca` guard lists `__linux__`/`__sun__`/`__EMSCRIPTEN__`/
  `__NEWLIB__` and not substrate — patch `0004` adds `__substrate__`.
  Substrate does ship `<alloca.h>`; stb simply had no way to know.

  2.0-RC4 adds `harfbuzz` for text shaping and drops `spandsp`, whose
  G.722 codec it now decodes in-tree; it also vendors SheenBidi.  Its
  `src/Makefile.am` puts `$(HARFBUZZ_CFLAGS)` and `$(FREETYPE_CFLAGS)`
  ahead of `CXXFLAGS`, so `build.sh` hands configure both rewritten into
  the sysroot rather than letting pkg-config's `-I/usr/include/…` name the
  build host's headers.  It does the same for `DIALOG_CFLAGS`, the GTK 2
  file dialog's flags, which reach `FileDialog.cpp` through per-target
  `CPPFLAGS` and so also precede `CXXFLAGS`; on a host with GTK 2
  installed the dialog was compiled against the host's headers.

  It is built as a unity build (`--enable-final`, all C++ sources in
  `src/psymp3.final.cpp`).  Patch `0005` extends the unity file's pugixml
  pragma to `-Wuninitialized`, the name GCC 16 gives a false positive
  upstream already silences as `-Wmaybe-uninitialized`.
- **fdk-aac 2.0.3** (`contrib/fdk-aac/`) — the Fraunhofer FDK AAC codec
  library, encoder and decoder.  It is PsyMP3's AAC path as of 2.0-RC3,
  which asks for `fdk-aac` where 1.99.16 asked for `faad2`; the two
  coexist, so nothing still linking `-lfaad` is affected.  CMake-built
  like faad2.
  Note the licence is the Fraunhofer Android one, not a standard
  free-software licence, and it grants no patent rights — see
  `contrib/fdk-aac/README.SUBSTRATE.md`.
- **opusfile 0.12** (`contrib/opusfile/`) — the high-level Ogg Opus
  decoder layered on `libogg` + `libopus`.  Ported because `sox`'s
  `src/opus.c` includes `<opusfile.h>`, which `libopus` does not ship;
  without it `contrib/sox` passed `--without-opus` and had no Opus
  support at all.  Worth remembering as a shape: an optional codec whose
  probe fails is not a build error, so a green log said nothing.
  Configured `--disable-http` to avoid pulling in openssl for URL
  streaming nothing uses.  Both this port and `sox` pass an explicit
  `-I<sysroot>/include/opus` — see the subdirectory-`Cflags` note under
  "Cross-build traps" — because `opusfile.pc` resolves that directory to
  the build host's `/usr/include/opus`.
- **mpg123** (`contrib/mpg123/`).
