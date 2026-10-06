# SunOS 4 (Sun386i) host tools

Host-side tools for the SunOS personality (`PERS_SUNOS`, rooted at
`/perso/sunos`).  All run on the Linux build host, not on substrate.

**They need distribution media that is not in this repository.**  Each says
at the top what to obtain and how to pass it in.  None is part of any build
or test.

| tool | what it does |
| --- | --- |
| `build-image.sh` | build a populated `/perso/sunos` ext2 image from the floppies of SunOS 4.0 for the Sun386i |
| `bar.py` | read Sun `bar(1)` archives (title, list, extract) -- the format of those floppies |

## The media

Written against SunOS 4.0.1 for the Sun386i (November 1988), 35 3.5"
floppies, as archived by Tenox (`tenox.pdp-11.net/os/sunos/sun386i/`,
`SunOS 4.01 Sun386i.rar`).

- Two **boot floppies** (a 4.2BSD filesystem and its continuation) that
  start the installation, and a **diagnostics** floppy.  Not used.
- Everything else is **bar archives**, one archive over several floppies.
  A volume starts with a header carrying the archive's title and the
  volume's number, and the size of what it finishes of the file the
  previous volume ended inside; `bar.py` has the layout.  Files are stored
  `compress`ed.  Five archives make the system:

  | title | goes to | floppies |
  | --- | --- | --- |
  | root file system | `/` | 2 |
  | /usr file system | `/usr` | 8 |
  | /files file system | `/files` | 1 |
  | Applications Supplement | `/files/cluster/sun386.sunos4.0.1/appl` | 7 |
  | Developer's Toolkit | `/files/cluster/sun386.sunos4.0.1/devel` | 7 |

  The last two are the optional *clusters* -- `doc_prep`, `sysV_commands`,
  `man_pages`, `games`, `base_devel` (the C compiler and libraries),
  `sunview_devel` and so on, a directory each.  `/usr` has a symbolic link
  for every file of every cluster, through `/usr/cluster`, so a program is
  at its usual path once its cluster is loaded.
- A **SunOS 4.0.2 Upgrade Kit** (7 floppies, also bar).  Not applied: its
  `install_update` script chooses files by what is installed.

## What the image is and is not

    mkdir sunos401
    bsdtar xf "SunOS 4.01 Sun386i.rar" -C sunos401
    tools/sunos/build-image.sh -m sunos401 -o sun386i.img

About 7500 files in a 192 MB image, with the archives' modes and owners.
`/usr/cluster` is a link to the cluster directory where a Sun386i mounts
it; links to absolute paths are made relative; there are no device nodes.
Some fifty links dangle, nearly all in the installer's prototype tree
(`/usr/etc/install/proto`) or pointing at devices.

## What the programs are

Not a.out: the Sun386i's programs are i386 COFF (`f_magic` 0x14c, optional
header magic 0413, text at 0x10d0 for the file's 0xd0, data on the next
page boundary).  Nearly all are dynamically linked, in SunOS's way and not
System V's: the kernel loads the program alone, and its startup code opens
and maps `/lib/ld.so`, which maps `/usr/lib/libc.so.2.0`.  `f_flags` has
0x800 set on such a program.

System calls are `int $0xff` with the SunOS 4 call number in `%eax` and the
arguments on the stack as for a C call; the carry flag reports an error,
with `errno` in `%eax`.  The numbers and the semantics are 4.3BSD's with
Sun's additions (`mmap`, `getdents`, `sigvec`, NFS).
