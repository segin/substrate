# gdb on Substrate

Port of **GNU gdb 16.2** to run natively on Substrate and debug Substrate
processes through the kernel's `ptrace(2)` syscall.

Layout follows the other contrib ports: `fetch.sh` downloads + sha-verifies +
extracts + applies `patches/` (listed in `series`); nothing under `build/` is
vendored.

## Status

| Piece | State |
|-------|-------|
| Kernel `ptrace(2)` syscall (the prerequisite) | **Done & tested** — see `sys/kern/ptrace.c`. TRACEME, ATTACH, PEEK/POKE TEXT+DATA, GET/SETREGS, CONT, SINGLESTEP, DETACH, KILL all implemented; cross-process register + memory inspection verified end-to-end. |
| `include/sys/ptrace.h` ABI (PTRACE_* + `user_regs_struct`) + libsys wrapper | **Done** |
| `config.sub` accepts `*-substrate` | **Done** — `patches/0001-config-sub-substrate.patch` (validated: `./config.sub i386-unknown-substrate` resolves). |
| bfd substrate object vecs in gdb's bundled `bfd/` | **TODO** |
| gdb native target wiring (`configure.host` / `configure.nat`) | **TODO** |
| `gdb/substrate-nat.c` (ptrace backend) | **TODO** |
| C++ Canadian-cross build (host = substrate) | **TODO** |

The hard kernel half — the syscalls a debugger needs to inspect another
process — is finished and proven (a tracer reads a stopped child's registers
and memory across the address-space boundary, writes memory back, single-steps,
and continues). What remains is the gdb userland port, scaffolded here.

## Remaining work (concrete)

1. **bfd substrate vecs.** gdb-16.2 bundles its own `bfd/`. Port the binutils
   substrate hunks (`contrib/binutils/patches/0002..0006`) — the
   `elf32-i386-substrate` / `elf64-x86-64-substrate` output vecs, the
   `targ_selvecs`, and the `bfd/config.bfd` + `bfd/configure` wiring — onto
   gdb-16.2's tree. (Contexts differ from binutils 2.46.0, so regenerate each
   hunk; same approach as `0001` here.)

2. **Native target selection.** Add substrate cases:
   - `gdb/configure.host` (model line: `i[34567]86-*-linux*) gdb_host=linux ;;`
     at configure.host:111) → `i[34567]86-*-substrate*) gdb_host=substrate ;;`
     plus a `gdb/config/i386/substrate.mh` listing the nat objects.
   - `gdb/configure.nat` (model: the `i386-linux-nat.o x86-linux-nat.o ...`
     block at configure.nat:258) → a substrate case selecting
     `inf-ptrace.o fork-child.o substrate-nat.o`.

3. **`gdb/substrate-nat.c`** — the native backend, modeled on
   `gdb/i386-bsd-nat.c` + `gdb/x86-bsd-nat.c` on top of the generic
   `gdb/inf-ptrace.c`:
   - `fetch_registers` / `store_registers`: `PTRACE_GETREGS` / `PTRACE_SETREGS`
     into/out of a `struct user_regs_struct` (the layout in
     `include/sys/ptrace.h`), mapped to the i386 regcache by register number.
   - memory r/w: `inf-ptrace.c`'s default `PTRACE_PEEKTEXT` / `POKETEXT` path
     already matches this port's ptrace (the libsys wrapper re-exposes the
     classic "PEEK returns the word" convention, which inf-ptrace expects).
   - `wait`: substrate reports a ptrace stop as `WIFSTOPPED` with `WSTOPSIG`
     the delivering signal (already what `wait4(WUNTRACED)` returns here).
   - single-step: `PTRACE_SINGLESTEP` (kernel sets EFLAGS.TF); software
     breakpoints work via `POKETEXT` of `int3`.

4. **Build.** gdb is C++, so the native build is a Canadian cross with the
   stage-2 substrate toolchain. Prereqs: a static `libstdc++.a` (substrate
   ships one; see the toolchain notes), `libgmp` (already a contrib dep of
   other ports), and optionally `libmpfr`/`libexpat`/`libncurses` (all
   present or stubbable). Configure roughly:

   ```
   ../gdb-16.2/configure \
       --host=i386-unknown-substrate --target=i386-unknown-substrate \
       --prefix=/usr --disable-gdbserver --disable-werror \
       --with-static-standard-libraries --without-guile --disable-tui
   ```

   then `make all-gdb` and stage `gdb/gdb` into
   `dist-overlay/dist-gdb/usr/bin/`.

## Why ptrace first

gdb is only as useful as the kernel facility under it. Substrate's `sys_ptrace`
was a `-ENOSYS` stub; it is now a real implementation built on the existing
job-control stop, `wait4`, trapframe, and `pmap_copyin/out_other` machinery, so
the gdb backend above is thin glue rather than new kernel work.

## Verified build recipe (gdb runs on substrate)

gdb 16.2 now builds as a dynamic substrate executable and runs (reads symbols,
sets breakpoints, catches the ptrace exec-stop, backtraces).  What it took, on
top of the kernel ptrace work:

**Dependencies (into the cross sysroot `${SR}=/opt/substrate/i386-unknown-substrate`):**
- `libgmp.so` — substrate's dynamic gmp; the gmp port now drops `libgmp.la`
  from the sysroot so libtool links the substrate `.so`, not the host's.
- `libmpfr.a` — built static from gcc's bundled `mpfr-4.2.2`
  (`configure --host=i386-unknown-substrate --with-gmp=${SR} --disable-shared`).
- libc gained the small-object `__atomic_*` outline functions (`lib/c/src/atomic.c`).

**gdb tree (in `build/gdb-16.2`, applied by hand — capture as patches when stable):**
- `config.sub`: accept `*-substrate` (`patches/0001`).
- `bfd/`: the binutils substrate ELF vecs (binutils `patches/0002..0006`) plus
  an `x86_64-*-substrate*` case in `bfd/config.bfd`.
- `gdb/configure.host`: `i[34567]86-*-substrate*) gdb_host=substrate ;;`.
- `gdb/configure.nat`: a `substrate` case → `inf-ptrace.o fork-child.o
  nat/fork-inferior.o` + cpu `i386` adds `substrate-nat.o`.
- `gdb/substrate-nat.c`: the native backend (saved here as `substrate-nat.c`) —
  an `inf_ptrace_target` subclass that maps gdb's i386 regs to/from
  `struct user_regs_struct` via PTRACE_GETREGS/SETREGS; memory + run control
  come from generic `inf-ptrace`.

**configure + build:**
```
../gdb-16.2/configure --build=$(../gdb-16.2/config.guess) \
    --host=i386-unknown-substrate --target=i386-unknown-substrate \
    --prefix=/usr --disable-gdbserver --disable-werror --disable-nls \
    --without-guile --disable-tui --disable-source-highlight --disable-shared \
    --with-static-standard-libraries --with-gmp=${SR} --with-mpfr=${SR}
# remove the misdirecting sysroot .la so -lgmp/-liconv resolve the substrate .so:
rm -f ${SR}/lib/lib{gmp,iconv,charset}.la
make all-gdb -k CXXFLAGS="-g -O2 -fpermissive"
strip gdb/gdb -o gdb && printf '\100' | dd of=gdb bs=1 seek=7 count=1 conv=notrunc  # OSABI
```
`-fpermissive`: substrate's `<sys/ptrace.h>` types the data arg `void*` while
inf-ptrace passes int/long (size-safe on i386).  The ptrace prototype is wrapped
in `extern "C"` so the C++ caller links the libsys symbol.

**Remaining:** software-breakpoint insertion into a dynamic program's
not-yet-resident `.text` at the exec-stop (the page isn't fault-resolvable
cross-process at that instant); capture the hand-applied tree changes as a
patch series.
