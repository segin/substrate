# Planned x86 Unix Personality Targets

The following execution personalities are planned or implemented for Substrate:

- **Substrate native ABI** (Primary)
- **Linux** (Active)
- **FreeBSD** (Active)
- **NetBSD**
- **OpenBSD**
- **Solaris / SVR4 family**
- **SunOS 4.x (Sun386i)**
- **SCO Unix** (`SCO-U/3.2v2`, `SCO-U/ODT3`, `SCO-OSR5`)
- **iBCS2** compatibility targets
- **ELKS** (16-bit Linux-like)
- **Minix a.out** compatibility path

## Xenix

Xenix is one personality: `Xenix`, id 131 (`PERS_XENIX`), rooted at
`/perso/xenix`, implemented by `exec/formats/xout.c` and
`exec/perso/perso_xenix.c`.

The `x.out` executable format spans three processors under a single magic
number, and there are two system call interfaces behind it: 8086 and 80286
images are 16-bit segmented programs that trap through `int $5` with
register arguments, while 80386 ones are 32-bit and use the System V
`lcall $7,$0` gate.  The personality has a half for each, chosen by the
process's word size, and the loader has a loader for each, chosen by the
header's `x_cpu`.

It used to be six ids -- `SCO-X/386` (131), `SCO-X/286` (132), and four
reserved for the 8086 and the Microsoft-branded releases (133-136) -- on
the theory that each vendor/processor pairing would need its own ABI.  It
did not turn out that way.  Every 8086 and 80286 binary tried, from SCO
Xenix 86 2.1.3, SCO Xenix 286 2.3.2 and IBM PC Xenix 1.00, runs under the
same 16-bit half; and a Xenix/386 installation is one tree holding all
three kinds of binary (2.2.3 ships 248 8086 programs, 5 80286 and 14 80386),
in which a program of one kind execs another.  Separate personalities meant
separate roots, which that tree cannot be split across.

| Half   | Programs       | Entry                | State                         |
|--------|----------------|----------------------|-------------------------------|
| 16-bit | 8086, 80286    | `int $5`             | Working                       |
| 32-bit | 80386          | `lcall $7,$0`        | The shell and basic utilities |

The personality is documented in `usr.man/man4/xenix.4`; the 16-bit
executable format in `usr.man/man4/xout286.4`.

## System V Release 4

`SVR4`, id 4 (`PERS_SVR4`), rooted at `/perso/svr4`: i386 ELF programs of
UNIX System V/386 Release 4 -- AT&T's reference port and what was made
from it.  Intel's Release 4.0 Version 2, the one tested, is that port
nearly unchanged, and needed nothing of its own.

The binaries are unbranded (OS/ABI 0, no note), so the ELF loader goes by
the interpreter, `/usr/lib/libc.so.1`, or, for a static program, by its
being under the personality's root.  The system call interface is
Xenix/386's -- `lcall $7,$0`, stack arguments, carry for errors, EDX for a
second result, a libc trampoline and `lcall $0xf,$0` out of a signal
handler -- and calls 1 to 63 are numbered alike.  That much belongs to
neither personality and is in `exec/perso/perso_sysv386.c`; each describes
what is its own (signal and open-flag numbering, the calls it adds) with a
`struct sysv386_abi`.  What Release 4 added is in
`exec/perso/svr4/svr4_calls.c`.

## System V Release 3

`SVR3`, id 5 (`PERS_SVR3`), rooted at `/perso/svr3`: the i386 COFF programs
of UNIX System V/386 Release 3 and of what was built on it (INTERACTIVE
UNIX).  `exec/formats/coff.c` loads them -- a ZMAGIC COFF file is mapped as
it lies, text in the first page and data 4 MiB up, with nothing to
relocate -- and maps the static shared libraries (`/shlib/libc_s`, at
0xa0000000) a program names in its `.lib` section.

The calls are Release 4's: Release 4 kept Release 3's as they were, so
`svr4_calls.c` serves both, with a second `struct sysv386_abi` for what a
Release 3 program is told and how its signal handlers are entered.

State: against AT&T Release 3.2.3 and INTERACTIVE UNIX 3.0, the shells and
utilities run and each system's C compiler compiles and links a program
that runs.  Documented in `usr.man/man4/svr3.4`.

Three things about where these programs live, which the vendors' own C
compilers were the first to need:

- **They work in their tree.**  Substrate looks an existing file up under
  a personality's root first and falls back to its own; that is all the
  generic lookup does, so a name that did not exist yet was created in
  substrate's root, and `mkdir`, `unlink`, `rename`, `chmod`, `chown` and
  `chdir` never looked under the tree at all.  A program could read
  `/export/x` and not write `/export/y`.  For these two personalities
  (`works_in_tree`) every path a system call is given is translated on the
  way in (`sysv386_string()`): if the file is under the tree, or the
  directory it would be made in is, the name under the tree is what is
  used.  Arguments and what a symbolic link says are not paths and are
  left as they are.  `pwd` therefore shows where a program really is,
  `/perso/svr4/...`.
- **`/dev` is substrate's** (`native_dev`), whatever the tree has there.
- **Address 0 can be read** in a Release 4 process, as one page of zeroes.
  Release 4 on the 386 allows it and programs depend on it: the code
  generator of the C compiler and the assembler both test a field through
  a pointer that may be null.  Without the page `cc` could compile a
  declaration and not an expression.  The ELF loader maps it read-only
  (`SVR4_PAGE_ZERO_SIZE`); Release 3 programs have their text there.

Checked by `tests/perso/svr4/treetest.c`.  With them, Dell UNIX's `cc` and
`cc -O` (AT&T's compiler, assembler and link editor) and its GCC 2.1 each
compile, link and run a program using stdio, `malloc` and floating point.

Neither release has socket calls; both reach the network, and the X
server, through STREAMS devices.  `exec/perso/svr4/svr4_streams.c`
provides the client's half of that over substrate's sockets, and with it
Dell UNIX's X11R5 clients and INTERACTIVE UNIX's X11R4 clients run against
substrate's X server.  See `sysv_streams_transport.md`.

State of Release 4: the vendor's static `/sbin/sh`, and `ksh` and the basic utilities
dynamically linked against the vendor's own `libc.so.1`, run -- Intel's
Release 4.0 Version 2 and Dell UNIX SVR4 Issue 2.2 alike (`tools/svr4`
builds an image from either).  Documented
in `usr.man/man4/svr4.4`.  Solaris x86 is the same family with a different
interpreter (`/usr/lib/ld.so.1`) and is not wired up.

## SunOS 4 (Sun386i)

`SunOS`, id 129 (`PERS_SUNOS`), rooted at `/perso/sunos`.  Not running
yet; this is what it has to be, from the SunOS 4.0.1 distribution
(`tools/sunos` builds a root from the floppies).

SunOS 4 is 4.3BSD with Sun's additions, and the personality is a BSD one:
its own call table, `sigvec` signals, BSD `stat`, `getdirentries` and
`getdents`, `mmap`.  It shares nothing with the AT&T personalities but a
file format.  A Sun386i program is an i386 COFF file, not the a.out the
other Suns use -- `aout.c`'s check for a Sun386 machine id never matches
anything the system ships -- so `exec/formats/coff.c` has to tell a
Sun386i program from a Release 3 one and hand it to this personality, and
never to `SVR3`:

- **Layout.**  Optional-header magic 0413, text at 0x10d0 for file offset
  0xd0, data on the next page boundary -- not Release 3's 4 MiB gap, and
  no `.lib` section.
- **Dynamic linking** is SunOS's.  The kernel loads the program alone;
  the program's startup code opens `/lib/ld.so` and maps it, and `ld.so`
  maps `/usr/lib/libc.so.2.0`.  Such a program has 0x800 set in `f_flags`.
  So the loader needs no interpreter support, and `open`, `read`, `mmap`
  (of a file, at a fixed address, private) and `close` have to be right
  before anything prints.
- **System calls** are `int $0xff`: the call number in `%eax`, the
  arguments on the stack as for a C call, carry set and `errno` in `%eax`
  on failure.  `perso_sunos.c` today is a table for a register-argument
  entry that no Sun386i program uses.
