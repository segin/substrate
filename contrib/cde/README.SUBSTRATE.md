# CDE (Common Desktop Environment) — substrate port

CDE is the classic Motif desktop — dtwm, dtsession, dtlogin, dtfile,
dtterm, dtpad, dtcalc, dtmail, dtstyle, dthelp, ToolTalk, dtksh — open
sourced as [cdesktopenv](https://sourceforge.net/projects/cdesktopenv/).

## Source

`fetch.sh` clones the cdesktopenv git repository, pinned to a specific
commit on the **`C23-GCC15-Changes`** branch.  There is no release tarball
to SHA-verify, so the commit is the reproducibility anchor.  That branch
rather than `master`: substrate's toolchain is GCC 16, whose C23 default
rejects the empty-paren prototypes the 30-year-old CDE sources are full of,
and the branch is upstream's fix for exactly that.

The modern cdesktopenv build is autotools (`autogen.sh` → `configure` →
`make`).  imake is gone — 0 Imakefiles remain — which is what makes
cross-compiling tractable at all.

`fetch.sh` applies the `patches/` series **before** `autogen.sh`, so the
generated `configure` and `Makefile`s come out already correct.  Only two
fixups run afterwards, because their targets are generated files rather than
source: `config.sub` learning the substrate triple, and libtool's `host_os`
case arms learning that `substrate*` behaves like `linux*`.

## The actual problem: generators

Cross-compiling CDE is not hard because of the compiler.  It is hard because
CDE builds roughly two dozen small programs and then **runs them, mid-build**,
to generate source files, message catalogs, ToolTalk type databases, font
aliases and help volumes.  Cross-compiled, every one of those comes out as an
i386-substrate binary that the build host cannot execute, and the build stops
with `Error 126`.

This port answers that once rather than case by case:

* `hosttools/build.sh` builds a complete **native** objdir of the same CDE
  tree at `hosttools/cde-host` — every generator, compiled for the build host,
  in the same relative location it occupies in the cross tree.
* `build.sh` points CDE's own generator variables at that tree.  Because
  automake defines `subdir` in every `Makefile`, one set of command-line
  variables redirects every generator in every directory:

  ```
  make CDE_HOST=<native objdir> \
       ELTDEF='$(CDE_HOST)/$(subdir)/eltdef' \
       LINETODATA='$(CDE_HOST)/$(subdir)/../util/lineToData' \
       ...
  ```

Nothing is copied into the cross tree and nothing races make's timestamps —
the cross build simply never runs a target binary.  Upstream already keeps
most of these paths in variables (`GENCPP`, `DTCODEGEN`, `TT_TYPE_COMP`,
`MERGE`, `MKCATDEFS`, `MSGSETS`, `TREERES`); patch 0007 does the same for the
handful that were still hardcoded.

`hosttools/build.sh` also builds, from source into a self-contained prefix,
the ordinary build-host programs CDE's configure looks for: `rpcgen`
(rpcsvc-proto), `ksh` (host mksh), `compress` (ncompress), `sessreg`,
`mkfontdir` (mkfontscale), `bdftopcf`, `onsgmls` (OpenSP), and CDE's own
`tradcpp`.

**dtcodegen** is the one generator that needs something the build host must
supply itself: it links Motif, so the native objdir can only build it if the
host has a Motif of its own (e.g. Arch's `openmotif`).  Without one,
dtappbuilder and ttsnoop do not build; everything else does.

## dtksh, and the one thing that is not settled

dtksh is the only program whose build is not CDE's own: it drives AST's
`package`/`mamake` system over the bundled ksh93.  That needs two things the
generator redirection above does not cover, both handled by a build.sh phase
that runs before make descends into `programs/dtksh`:

1. **A compiler intercept that tells the two halves apart.**  AST compiles the
   product (ksh93 and its libraries) *and* its own build machinery — mamake,
   proto, probe, ratz — with the same `$CC`.  The machinery has to run on the
   build host.  Sending it through the cross compiler is what produces
   `mamake: Accessing a corrupted shared library` partway through the build,
   and it is not obvious from the error that a build tool rather than the
   product is at fault.  `src/cmd/INIT/cc.linux.i386` routes by name.
2. **A native mamake in the target arch tree.**  `package` prepends
   `$INSTALLROOT/bin` to PATH and runs mamake from there, so a host binary in
   `arch/linux.i386-64/bin` is not enough.

With those, ksh93 cross-builds and dtksh links as an i386-substrate binary —
**but only in one of the two configurations, and neither is complete:**

* Run AST's `package` **without** `CCFLAGS=$(KSH93_SHOPTS)` and ksh93 builds
  cleanly, but its libshell is then compiled with different SHOPT settings
  from dtksh's own objects.  dtksh's link fails on `liblist`, a symbol
  `SHOPT_DYNAMIC` guards.  `programs/dtksh/Makefile.am` says this out loud:
  *"It is vital that dtksh and ksh93 are built with the same options."*
* Pass `CCFLAGS=$(KSH93_SHOPTS)` — what the Makefile intends, and what
  build.sh does — and the AST build dies earlier, in libast's `FEATURE/lib`:
  an iffe output-block probe fails to compile, `arca<nnnnn>.c:1:2: error:
  expected identifier or '(' before ';' token`.  The generated probe source
  is malformed at its very first line, which points at how the flags reach
  iffe rather than at anything substrate-specific.

So **dtksh does not currently build**, and `build.sh` configures it out.
This is a regression against the previous port, which shipped a `dtksh`
binary.  Everything else in CDE builds.  `CDE_DISABLE_DTKSH=1` forces the
same behaviour explicitly.

**What is not confirmed:** iffe — AST's feature prober — compiles small
programs and RUNS them, so ksh93's FEATURE headers describe whichever machine
executed the probe.  `hosttools/crossexec.d/crossexec` exists to make that
machine substrate: it boots a private copy of the root image headlessly in
qemu, runs the probe there and relays stdout and exit status back over an
`@@IFFE@@`-framed serial protocol.  Standalone it works — it runs a
cross-compiled binary and returns both its output and its exit code.  But
across a full ksh93 build (151 probes, `IFFEFLAGS="-x linux.i386"`, crossexec
on PATH and installed by AST into its own arch tree) **no qemu boot was
observed**, so the run-type probes appear to be answered without executing
anything.  The compile- and link-type probes are accurate for the target
regardless; the run-type answers should be treated as defaults until someone
demonstrates otherwise.  dtksh builds and links either way.

## The patch series

| patch | what |
|---|---|
| 0001 | `configure.ac`: select the OS from `host_os`, not `build_os` (only the former means anything in a cross build), and add a `substrate*` arm. |
| 0002 | ttsession: list libtt again after libstt — upstream relies on libtt being shared. |
| 0003 | libABil: give its yacc globals a private prefix; they collide with Motif's libUil when both are static. |
| 0004 | ttsnoop: rename its private `_tt_sigset`, which libtt also defines. |
| 0005 | dtappbuilder: link `-lMrm` directly — `MRESOURCELIB` is referenced but never substituted. |
| 0006 | dtdocbook/instant: keep the Tcl paths as make variables so a cross build can point them at its sysroot instead of the host's `/usr`. |
| 0007 | Make the in-tree build-time generators overridable (see above). |

Patch 0001 also defines `__linux__` and `linux` for the substrate target.
CDE's sources select between a modern code path and a pile of SVR4/SunOS-era
declarations on those predefines, and substrate — pthreads, ELF, BSD sockets,
a glibc-shaped libc — wants the modern one.  Without them CDE pulls in
declarations that clash with substrate's own headers (its `extern ioctl`, the
BSD-only `SO_USELOOPBACK`, ...).

## Prerequisite ports

`contrib/motif` (libXm/libMrm/libUil), the X client stack (libX11, libXt,
libXext, libXmu, libXpm, libXaw, libICE, libSM, libXinerama, libXScrnSaver),
plus `libjpeg`, `lmdb`, `tcl`, `libtirpc` (Sun RPC, for ToolTalk) and `mksh`
(the target's `/bin/ksh`).  `build.sh` merges their `dist-overlay/dist-*`
trees into one sysroot and fails early, naming what is missing, if any are
absent.

## Building

```sh
./fetch.sh          # clone at the pinned commit, patch, autogen
./build.sh          # builds hosttools on first run, then cross-builds
```

Output is staged into `dist-overlay/dist-cde` (CDE installs under `/usr/dt`).

`--disable-docs` is passed: the `doc/` tree renders CDE's manual pages by
running the freshly built `dtdocbook` and `instant` — programs, not
generators with an overridable path, so there is nothing to redirect at the
native objdir.

Shebang rewriting is the last step.  CDE bakes the ksh it found at configure
time into every generated ksh script (`dtsession_res`, `dtappintegrate`,
`dtopen`, the `Xsession.d` fragments, `Xsetup`, ...).  That is the hosttools
mksh, at a path that does not exist on the target, so the kernel's shebang
handler fails with `ENOENT` and, for instance, `dtsession_res` cannot xrdb the
CDE resources at session start.  They are rewritten to `/bin/ksh`.

## Substrate fixes CDE needed

These are in the kernel and libraries, not here, but CDE is what surfaced
them:

* the ld.so canonical-PLT fix (function-pointer equality) — without it dtwm
  aborts building the Front Panel with "Unresolved inheritance operation";
* the libc `MB_CUR_MAX` fix — it was hardcoded to 4 while substrate is a
  single-byte locale, so dtterm took the `wchar_t`/`XwcDrawString` path and
  drew every ASCII cell as a glyph plus three tofu boxes;
* `SO_PEERCRED` in the AF_UNIX `getsockopt` — DCOP-style peer credential
  checks fail without it;
* `extern "C"` guards in `include/sys/times.h`.  It declared
  `clock_t times(struct tms *)` bare, so every C++ translation unit that
  included it referenced a mangled `times(tms*)` that libc — which exports
  plain C `times` — cannot satisfy, and `libDtMmdb.so` failed to link into
  dtinfo.  Nine further headers have the same defect and are **not** fixed
  here: `sys/{ldt,mount,random,sysctl,sysinfo}.h`, `endian.h`, `exvi.h`,
  `ifaddrs.h`, `modeparse.h`.  Each will bite the first C++ consumer that
  includes it.

## What this stages, measured against the previous port

2359 files, of which 2113 are also in the previous port's tree.  The
differences:

* **+9 binaries** — the whole `dthelp` cluster the previous port deferred:
  `dthelptag`, `dthelpview`, `dthelpgen`, `dthelpprint`, `dthelp_htag1`,
  `dthelp_htag2`, `dthelp_ctag1` and the two driver scripts.
* **+`types.xdr`** — the compiled ToolTalk type database.  The previous port
  shipped the nine raw `.ptype` sources and no compiled database, i.e. it
  never ran `tt_type_comp`; this one does.
* **−`dtksh`** and its `DtFuncs.dtsh` — the regression described above.
* **−`appconfig/types/C/action`** — not a loss: upstream's `types.am` says
  `# we do not want to install 'action'`.  The previous port installed it
  because its Python `merge` replica did.

The tree is also a third of the size (123 MB against 380 MB) because the
`libDt*` libraries are now genuine shared objects.  The previous port's
libtool fixup matched an older libtool's case arms and silently stopped
matching, so every one of its binaries carried its own static copy of
libDtSvc, libDtHelp and friends — 4.3 MB per binary against 0.6 MB here.

## Runtime notes

* ToolTalk needs the host's own name to resolve.  `/etc/hosts` must map the
  hostname to `127.0.0.1` or `dtsession` blocks in `tt_open()`.
