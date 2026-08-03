# CDE (Common Desktop Environment) — substrate port

CDE is the classic Motif-based Unix desktop (dtwm, dtsession, dtlogin,
dtfile, dtterm, dtpad, dtcm, dthelp, dtksh, ...), open-sourced as
[cdesktopenv](https://sourceforge.net/projects/cdesktopenv/).  The port
is **complete and live**: the full desktop cross-builds end-to-end and
comes up on substrate — dtsession starts ToolTalk, dtwm decorates
clients and draws the Front Panel, dtterm renders cleanly.

## Source

CDE is a git repository, not a release tarball, so `fetch.sh` pins an
exact upstream commit (`COMMIT` in `fetch.sh`) on the
**`C23-GCC15-Changes`** branch — substrate's toolchain is GCC 16 and the
30-year-old CDE sources only compile cleanly with that branch's
modern-compiler fixes.  The modern cdesktopenv build is **autotools**
(`autogen.sh` → `configure` → `make`); imake is no longer used.

Substrate source modifications live in `patches/` (applied by `fetch.sh`
per `series`, *before* `autogen.sh` so automake regenerates the affected
`Makefile.in`).  Post-`autogen` seds in `fetch.sh` (config.sub, the
libtool cases in `configure`) and post-`configure` seds in `build.sh`
(SUBDIRS deferrals, generated-Makefile path fixes) only ever touch
**generated** files, which a patch series cannot target reproducibly.

## Prerequisites

Target ports (all in `contrib/`): the X client stack
(xorgproto ... libXaw), **Motif 2.3.8**, **libXinerama**,
**libXScrnSaver** (libXss), **libjpeg**, **lmdb**, **Tcl**,
**libtirpc** (Sun RPC, ToolTalk's IPC transport), **mksh** (the target
`/bin/ksh`).  `crypt()` lives in substrate's libc — configure must not
add `-lcrypt`.

Build-host programs (`hosttools/build.sh`, staged into
`hosttools/prefix`): `rpcgen`, host mksh-as-`ksh`, `compress`
(ncompress), `sessreg`/`mkfontdir`/`bdftopcf` (X apps), `onsgmls`
(OpenSP), `tradcpp` (GENCPP), `dtcodegen-host` (native dtcodegen against
the build host's Motif, for dtappbuilder/ttsnoop), native CDE generator
tools in `prefix/cde-tools` (pmaker/dfiles/msgsets/mkdbd, for
dtinfo/dtdocbook), and `crossexec` (qemu-backed on-target probe runner
for dtksh's iffe).

## Build flow (`build.sh`)

1. Assembles a mini-sysroot from the prerequisite `dist-overlay/dist-*`
   trees + substrate's core `.so`s.
2. Configures in-source (`-D__linux__ -Dlinux`: CDE's Linux code paths
   are the right ones for substrate) with the GCC-16 error demotions.
3. Defers the clusters that need unrunnable cross-built generators
   (dtksh, dtappbuilder, ttsnoop, dtinfo, dtdocbook, tttypes, types,
   localized, dthelp) out of SUBDIRS, then builds the core desktop with
   host `tradcpp` as GENCPP and `-lsys -lstdc++ -liconv` appended.
4. Re-enters each deferred cluster with its host-built generator swapped
   in: dtcodegen-host (dtappbuilder + ttsnoop), cde-tools
   (dtinfo + dtdocbook), and the AST `package`/mamake harness with
   qemu-backed iffe probes via `crossexec` (dtksh — needs a baked
   rootfs-derived exec image, `SUBSTRATE_EXEC_IMG`, default
   `/tmp/sub-exec.img`).
5. Stages into `dist-overlay/dist-cde`, rewrites build-host ksh shebangs
   to `/bin/ksh`, and installs the C-locale datatype/action database +
   dtwm Front Panel via `install-localized-types.sh` (+ `cdemerge.py`, a
   `merge(1)` replica) since the `localized`/`types` clusters are
   deferred.

Still deferred (build-tooling, not desktop function): the tt_type_comp
compilation of the ToolTalk type DB (`build.sh` stages the `.ptype`
sources and runs the pure-GENCPP `.dt`/`.fp` generation in
`programs/types`, so the full DTTYPES set + `dtwm.fp` installs), the
rest of `localized` (only the C-locale types slice is staged), and the
dthelp document compiler (helptag SGML parser swarm).

## Substrate fixes the desktop depends on

- the ld.so canonical-PLT fix (function-pointer equality) — dtwm's
  front-panel widget class otherwise aborts with "Unresolved inheritance
  operation";
- the libc `MB_CUR_MAX` fix (single-byte locale) — dtterm otherwise
  takes the `XwcDrawString` path and draws tofu;
- the kernel AF_UNIX `msg_name` fix + an `/etc/hosts` hostname →
  127.0.0.1 mapping, or dtsession's `tt_open()` never reaches ttsession.

## Reproducing

```sh
./fetch.sh          # pinned clone + patch series + autogen
(cd hosttools && ./build.sh)   # build-host tools (once)
./build.sh          # sysroot + configure + full cross-build + staging
```

`build.sh` skips dtksh (no exec image) / dtappbuilder + ttsnoop (no host
Motif) / dtinfo + dtdocbook (no cde-tools) gracefully when their host
prerequisites are absent, and builds the rest of the desktop regardless.
