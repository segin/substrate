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

Xenix is not one target.  The `x.out` executable format spans three
processors under a single magic number, and the 8086/80286 images are 16-bit
segmented programs that trap through `int $5` with register arguments, while
the 80386 ones are 32-bit and use the System V `lcall $7,$0` gate.  The
Microsoft-branded releases predate SCO's and differ again.  Each pairing is
therefore its own personality:

| Personality  | id  | Status      | Loader / personality                              |
|--------------|-----|-------------|---------------------------------------------------|
| `SCO-X/386`  | 131 | Active      | `exec/formats/xout.c`, `exec/perso/perso_xenix.c` |
| `SCO-X/286`  | 132 | Active      | `exec/formats/xout286.c`, `exec/perso/perso_sco_x286.c` |
| `SCO-X/86`   | 133 | Reserved    | —                                                 |
| `MS-X/86`    | 134 | Reserved    | —                                                 |
| `MS-X/286`   | 135 | Reserved    | —                                                 |
| `MS-X/386`   | 136 | Reserved    | —                                                 |

`SCO-X/286` is documented in `usr.man/man4/sco_x286.4`; its executable format
is documented in `usr.man/man4/xout286.4`.
