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

State of Release 4: the vendor's static `/sbin/sh`, and `ksh` and the basic utilities
dynamically linked against the vendor's own `libc.so.1`, run.  Documented
in `usr.man/man4/svr4.4`.  Solaris x86 is the same family with a different
interpreter (`/usr/lib/ld.so.1`) and is not wired up.
