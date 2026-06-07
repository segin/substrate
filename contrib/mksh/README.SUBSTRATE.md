# mksh R59c — substrate port

The MirBSD Korn Shell: substrate's `/bin/ksh`.  Chosen because CDE's
`configure` accepts `ksh`/`ksh93`/`mksh` for its `KSH` build/runtime
dependency and mksh cross-builds trivially.  The full ksh93 desktop shell
(dtksh) is a separate thing — CDE builds it from its own bundled ksh93 93u+m.

## Build

```sh
./fetch.sh
./build.sh        # -> dist-mksh/bin/{mksh, ksh -> mksh}
```

## Substrate notes

- **No autotools.**  mksh's `Build.sh` probes the compiler; almost all probes
  are compile-only, so cross-compilation works.  We force `TARGET_OS=Linux`
  (substrate is ELF/Linux-shaped; mksh has no substrate OS profile) and link
  it as a **PIE** against `libc.so.0` + `libsys.so.0` through `/sbin/ld.so`,
  the same shape as substrate's in-tree `bin/` programs.  The output's OSABI
  byte is branded `ELFOSABI_SUBSTRATE` (0x40).
- **Stack protector.**  mksh defaults to `-fstack-protector-strong` and it is
  kept on.  This required two libc additions that landed alongside the port:
  the `__stack_chk_guard` canary (seeded from `AT_RANDOM` at startup) and,
  for PIE, the hidden `__stack_chk_fail_local` helper (provided by crt0,
  standing in for the toolchain's absent `libssp_nonshared.a`).
- **sigsetjmp fix.**  mksh leans on `sigsetjmp`/`siglongjmp` for its control
  flow and exit unwinding, which exposed a real libc bug: `sigsetjmp` had
  wrapped `setjmp` in a function call, saving the wrong (wrapper) frame.  That
  is fixed in libc (`<setjmp.h>` macro + `__sigjmp_save`), not worked around
  here.

## Verified

Runs ksh scripts correctly (loops, `$(( ))` arithmetic, `typeset -u`, the
`print` builtin, the `/bin/ksh` symlink) and propagates exit status
correctly (`exit 0`/`exit 5`, `false` -> 1, script `exit 7`), with no crash.
