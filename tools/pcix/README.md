# PC/IX host tools

Host-side tools for the PC/IX personality (`PERS_PCIX`, rooted at
`/perso/pcix`, `pcix(4)`).  Both run on the Linux build host, not on
substrate.

**They need distribution media that is not in this repository.**  Each says
at the top what to obtain and how to pass it in.  Neither is part of any
build or test.

| tool | what it does |
| --- | --- |
| `build-image.sh` | build a populated `/perso/pcix` ext2 image from the floppies of PC/IX 1.0 |
| `pcixbackup.py` | read those floppies: list or extract a System III filesystem image or an INTERACTIVE `backup` archive |

    7z x 'PC-IX 1.0 (5.25-360k).7z'
    tools/pcix/build-image.sh -m 'PC-IX 1.0 (5.25-360k)/IMA' -o pcix.img

Attach the image as a second disk and mount it at `/perso/pcix`:

    mkdir -p /perso/pcix
    mount /dev/storage/virtio0 /perso/pcix ext2
    /perso/pcix/bin/sh

## PC/IX programs must be under `/perso/pcix`

**A PC/IX program run from anywhere else is run as an ELKS program, and
fails.**  Keep PC/IX binaries in the tree, and copy one into it before
running it; do not run one from `/tmp` or a home directory of substrate's.

This is not a check that could be made smarter.  MINIX took its `a.out`
format from PC/IX, and ELKS has it from MINIX, so the three share one
header, and the kernel has only the header to go by:

- Most of what PC/IX ships (249 of the 260 programs on the media) is
  linked with pure text, flags `0x70`, which no ELKS program has.  Those
  are known for PC/IX programs wherever they are.
- The rest, and anything `cc` makes without `-n`, have flags `0x10` or
  `0x20`.  Five on the media (`/usr/bin/ffill` is one) have the 32-byte
  header, 8086 for the processor, version 0, entry 0 and a 64K total:
  every field one that a combined MINIX or ELKS program may have, and
  the ELKS loader takes it for one.  Nothing in the file says which
  system it is for.

So for those the kernel goes by where the program is, or by who runs it:
a file named under `/perso/pcix`, or executed by a process that is
already a PC/IX one, is PC/IX's.  A program started from a PC/IX shell is
therefore right wherever it lives; one started from substrate's own
shell has to be in the tree.

## The media

IBM Personal Computer Interactive Executive 1.0 (IBM and INTERACTIVE
Systems, 1984), nineteen 360K 5.25" floppies, as archived by WinWorld
("PC-IX 1.0 (5.25-360k)").  The archive has each floppy twice, as a raw
image (`IMA/`) and as ImageDisk (`IMD/`); the raw ones are read.

| floppies | what | format |
| --- | --- | --- |
| 01MAINT | standalone installation tools (`format`, `mkfs`, `fsck`, `install`, `sash`) | a bootable System III filesystem; not installed |
| 02CORE1 - 09CORE8 | the Core system: `/bin`, `/etc`, `/lib`, `/unix`, the C compiler | one `backup` archive **by inode** |
| 10PROG1 - 13PROG4 | Programming subset: `/usr/include`, libraries, `lex`, `yacc`, `lint`, `adb` | one `backup` archive by name |
| 14COMMS, 15SCCS, 16TEXT, 17SPECIA, 18GAMES, 19ACCNT | the other subsets | an archive by name each |

`backup` is INTERACTIVE's dump program, and the format is the one AIX kept:
records headed by a length in 8-byte words, a type and the magic 0xea6b.  A
multi-volume archive is one stream cut wherever a floppy filled, each
volume starting with a header that says how many words it carries.
`pcixbackup.py` has the layouts at the top; the system's own description is
`/usr/include/dumprestor.h` in the image.

The whole system is 6 MB.  Everything the personality was written from is
in it: `/usr/include/sys.s` documents the system-call interface,
`/usr/include/a.out.h` the executable format, `/usr/include/sys/*.h` the
structures.
