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
