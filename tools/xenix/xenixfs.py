#!/usr/bin/env python3
"""
xenixfs.py - read SCO Xenix / System V filesystem images on a Linux host.

Xenix distribution media (and the root filesystem of a Xenix install) use a
V7-derived filesystem that nothing on a modern host can mount: Linux dropped
sysvfs support for the Xenix variant long ago, and the on-disk details differ
from stock System V in ways that matter.  This reads it directly.

WHY THIS EXISTS
    The SCO Xenix 286 install floppy (rts/n1.img) is not a tar archive like
    the rest of the distribution -- it is a bootable Xenix filesystem, and it
    is the ONLY place /bin/sh ships.  Getting the shell (and cpio, dd, fsck,
    mv, cp, ln, ps, tar) out of it needs a reader for this format.

ON-DISK FORMAT (verified against SCO Xenix 2.3.2 media)
    Block size is 1024.  Layout:

        block 0        boot block
        block 1        superblock
        block 2..      inode list (i-list)
        block s_isize  first data block

    Superblock (only the leading fields are needed here):

        u16  s_isize    first data block  [see NOTE]
        u32  s_fsize    total blocks in the volume

    NOTE: stock V7 documents s_isize as the LENGTH of the i-list, making the
    first data block 2 + s_isize.  On this media it is the first data block
    itself: s_isize == 12 and the root directory lives at block 12, not 14.
    Reading inodes never depends on this (the i-list always starts at block
    2); s_isize is used only to bound the inode count and sanity-check.

    Inode (64 bytes, inode N at 2*1024 + (N-1)*64; root is inode 2):

        +0   u16  di_mode
        +2   i16  di_nlink
        +4   u16  di_uid
        +6   u16  di_gid
        +8   u32  di_size
        +12  13 x 3-byte block numbers, LITTLE-ENDIAN 24-bit
             (10 direct, then single / double / triple indirect)
        +51  atime, mtime, ctime (u32 each)

    The 3-byte addresses are plain little-endian here, NOT the PDP-11 "l3tol"
    middle-endian packing that V7 used on its original hardware.

    Indirect blocks hold u32 little-endian block numbers (256 per block).

    Directories are arrays of 16-byte records: u16 inode, then a 14-byte
    NUL-padded name.  A zero inode means the slot is free.

MBR
    A whole-disk image (as opposed to a single-partition floppy image) carries
    a DOS partition table.  Xenix owns types 0x02 (XENIX root) and 0x03
    (XENIX usr); this finds them and reads the filesystem at that offset.
    Without a valid 0x55AA signature the whole image is treated as one
    filesystem, which is what a floppy image is.

USAGE
    xenixfs.py partitions IMAGE
    xenixfs.py info       IMAGE [-p N | -o BYTES]
    xenixfs.py ls         IMAGE [PATH] [-R] [-p N | -o BYTES]
    xenixfs.py cat        IMAGE PATH  [-p N | -o BYTES]
    xenixfs.py extract    IMAGE DEST [--path PATH] [-p N | -o BYTES]

This file is part of Substrate.  ISC licensed.
"""

import argparse
import errno
import os
import stat
import struct
import sys

BLOCK_SIZE = 1024
INODE_SIZE = 64
INODES_PER_BLOCK = BLOCK_SIZE // INODE_SIZE
ILIST_START_BLOCK = 2
ROOT_INO = 2
DIRENT_SIZE = 16
NAME_LEN = 14

# MBR partition types Xenix claims.
PART_XENIX_ROOT = 0x02
PART_XENIX_USR = 0x03
XENIX_TYPES = (PART_XENIX_ROOT, PART_XENIX_USR)

PART_TYPE_NAMES = {
    0x00: "empty",
    0x01: "FAT12",
    PART_XENIX_ROOT: "XENIX root",
    PART_XENIX_USR: "XENIX usr",
    0x04: "FAT16 <32M",
    0x05: "extended",
    0x06: "FAT16",
    0x07: "HPFS/NTFS",
    0x0B: "FAT32",
    0x0C: "FAT32 LBA",
    0x0E: "FAT16 LBA",
    0x63: "GNU HURD / SysV",
    0x82: "Linux swap",
    0x83: "Linux",
}


class XenixError(Exception):
    pass


class Partition:
    def __init__(self, index, boot, ptype, lba_start, sectors):
        self.index = index
        self.boot = boot
        self.ptype = ptype
        self.lba_start = lba_start
        self.sectors = sectors

    @property
    def offset(self):
        return self.lba_start * 512

    @property
    def size(self):
        return self.sectors * 512

    @property
    def type_name(self):
        return PART_TYPE_NAMES.get(self.ptype, "0x%02x" % self.ptype)

    @property
    def is_xenix(self):
        return self.ptype in XENIX_TYPES


def read_mbr(data):
    """Return the primary partitions, or [] when there is no partition table."""
    if len(data) < 512 or data[510] != 0x55 or data[511] != 0xAA:
        return []
    parts = []
    for i in range(4):
        off = 0x1BE + i * 16
        boot, ptype = data[off], data[off + 4]
        lba_start, sectors = struct.unpack_from("<II", data, off + 8)
        if ptype == 0 or sectors == 0:
            continue
        parts.append(Partition(i + 1, boot == 0x80, ptype, lba_start, sectors))
    return parts


class Inode:
    __slots__ = ("num", "mode", "nlink", "uid", "gid", "size", "addr",
                 "atime", "mtime", "ctime")

    def is_dir(self):
        return stat.S_ISDIR(self.mode)

    def is_reg(self):
        return stat.S_ISREG(self.mode)

    def is_lnk(self):
        return stat.S_ISLNK(self.mode)

    def is_special(self):
        return not (self.is_dir() or self.is_reg() or self.is_lnk())


class XenixFS:
    """A Xenix filesystem inside `image`, starting at byte `offset`."""

    def __init__(self, image_path, offset=0):
        self.path = image_path
        self.offset = offset
        with open(image_path, "rb") as fh:
            fh.seek(0, os.SEEK_END)
            total = fh.tell()
        if offset >= total:
            raise XenixError("offset %d is past the end of %s (%d bytes)"
                             % (offset, image_path, total))
        self.fh = open(image_path, "rb")
        self.length = total - offset
        self._read_superblock()

    def close(self):
        self.fh.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- raw access ------------------------------------------------------
    def block(self, num):
        if num <= 0:
            return b"\0" * BLOCK_SIZE
        pos = self.offset + num * BLOCK_SIZE
        if pos + BLOCK_SIZE > self.offset + self.length:
            return b"\0" * BLOCK_SIZE
        self.fh.seek(pos)
        buf = self.fh.read(BLOCK_SIZE)
        return buf.ljust(BLOCK_SIZE, b"\0")

    def _read_superblock(self):
        sb = self.block(1)
        self.s_isize, self.s_fsize = struct.unpack_from("<HI", sb, 0)
        # See the NOTE in the module docstring: s_isize is the first data
        # block on this media.  Guard against a wild value before trusting it.
        if not ILIST_START_BLOCK < self.s_isize < self.s_fsize:
            raise XenixError(
                "not a Xenix filesystem at offset %d "
                "(s_isize=%d s_fsize=%d)" % (self.offset, self.s_isize,
                                             self.s_fsize))
        blocks_available = self.length // BLOCK_SIZE
        if self.s_fsize > blocks_available:
            raise XenixError(
                "superblock claims %d blocks but only %d are present at "
                "offset %d -- wrong offset or truncated image"
                % (self.s_fsize, blocks_available, self.offset))
        self.ilist_blocks = self.s_isize - ILIST_START_BLOCK
        self.num_inodes = self.ilist_blocks * INODES_PER_BLOCK

    # -- inodes ----------------------------------------------------------
    def inode(self, num):
        if num < 1 or num > self.num_inodes:
            raise XenixError("inode %d out of range (1..%d)"
                             % (num, self.num_inodes))
        blk = ILIST_START_BLOCK + (num - 1) // INODES_PER_BLOCK
        off = ((num - 1) % INODES_PER_BLOCK) * INODE_SIZE
        raw = self.block(blk)[off:off + INODE_SIZE]

        ino = Inode()
        ino.num = num
        (ino.mode, ino.nlink, ino.uid, ino.gid,
         ino.size) = struct.unpack_from("<HhHHI", raw, 0)
        a = raw[12:12 + 39]
        ino.addr = [a[k * 3] | (a[k * 3 + 1] << 8) | (a[k * 3 + 2] << 16)
                    for k in range(13)]
        ino.atime, ino.mtime, ino.ctime = struct.unpack_from("<III", raw, 51)
        return ino

    def _indirect(self, blknum, depth, want, out):
        """Append block numbers from an indirect block tree until `want`."""
        if blknum == 0 or len(out) >= want:
            return
        buf = self.block(blknum)
        for i in range(BLOCK_SIZE // 4):
            if len(out) >= want:
                return
            b = struct.unpack_from("<I", buf, i * 4)[0]
            if depth == 1:
                out.append(b)
            else:
                self._indirect(b, depth - 1, want, out)

    def block_list(self, ino):
        want = (ino.size + BLOCK_SIZE - 1) // BLOCK_SIZE
        out = list(ino.addr[:10])[:want]
        if len(out) < want:
            self._indirect(ino.addr[10], 1, want, out)
        if len(out) < want:
            self._indirect(ino.addr[11], 2, want, out)
        if len(out) < want:
            self._indirect(ino.addr[12], 3, want, out)
        return out[:want]

    def read_file(self, ino):
        data = bytearray()
        for b in self.block_list(ino):
            data += self.block(b)
        return bytes(data[:ino.size])

    # -- directories -----------------------------------------------------
    def readdir(self, ino):
        if not ino.is_dir():
            raise XenixError("inode %d is not a directory" % ino.num)
        out = []
        data = self.read_file(ino)
        for off in range(0, len(data) - DIRENT_SIZE + 1, DIRENT_SIZE):
            num = struct.unpack_from("<H", data, off)[0]
            if num == 0:
                continue
            name = data[off + 2:off + DIRENT_SIZE].split(b"\0")[0]
            out.append((num, name.decode("latin-1")))
        return out

    def lookup(self, path):
        """Resolve an absolute path to an Inode."""
        ino = self.inode(ROOT_INO)
        for part in [p for p in path.strip("/").split("/") if p]:
            if not ino.is_dir():
                raise XenixError("not a directory: %s" % path)
            for num, name in self.readdir(ino):
                if name == part:
                    ino = self.inode(num)
                    break
            else:
                raise XenixError("no such path: %s" % path)
        return ino

    def walk(self, ino=None, prefix="/"):
        """Yield (path, Inode) for everything under `ino`, depth first."""
        if ino is None:
            ino = self.inode(ROOT_INO)
        for num, name in sorted(self.readdir(ino), key=lambda e: e[1]):
            if name in (".", ".."):
                continue
            child = self.inode(num)
            path = prefix.rstrip("/") + "/" + name
            yield path, child
            if child.is_dir():
                yield from self.walk(child, path)


def mode_string(mode):
    kind = "?"
    for bit, ch in ((stat.S_IFDIR, "d"), (stat.S_IFREG, "-"),
                    (stat.S_IFLNK, "l"), (stat.S_IFCHR, "c"),
                    (stat.S_IFBLK, "b"), (stat.S_IFIFO, "p")):
        if stat.S_IFMT(mode) == bit:
            kind = ch
            break
    perms = ""
    for who in (6, 3, 0):
        perms += "r" if mode & (0o4 << who) else "-"
        perms += "w" if mode & (0o2 << who) else "-"
        perms += "x" if mode & (0o1 << who) else "-"
    return kind + perms


# ---------------------------------------------------------------------------
# Opening: partition selection
# ---------------------------------------------------------------------------

def open_fs(args):
    with open(args.image, "rb") as fh:
        head = fh.read(512)
    parts = read_mbr(head)

    if getattr(args, "offset", None) is not None:
        return XenixFS(args.image, args.offset)

    if getattr(args, "partition", None) is not None:
        for p in parts:
            if p.index == args.partition:
                return XenixFS(args.image, p.offset)
        raise XenixError("no partition %d in %s" % (args.partition, args.image))

    if not parts:
        return XenixFS(args.image, 0)      # floppy / bare filesystem image

    xenix = [p for p in parts if p.is_xenix]
    if len(xenix) == 1:
        return XenixFS(args.image, xenix[0].offset)
    if len(xenix) > 1:
        raise XenixError(
            "%s has %d Xenix partitions; pick one with -p "
            "(see `xenixfs.py partitions`)" % (args.image, len(xenix)))
    # A partition table with nothing Xenix in it: the image may still be a
    # bare filesystem whose block 0 merely looks like an MBR.  Try offset 0.
    return XenixFS(args.image, 0)


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------

def cmd_partitions(args):
    with open(args.image, "rb") as fh:
        head = fh.read(512)
    parts = read_mbr(head)
    if not parts:
        print("no MBR partition table (bare filesystem image)")
        return 0
    print("%-3s %-4s %-12s %12s %12s %10s" %
          ("#", "boot", "type", "start(LBA)", "sectors", "size"))
    for p in parts:
        print("%-3d %-4s %-12s %12d %12d %9dM%s" %
              (p.index, "*" if p.boot else "", p.type_name, p.lba_start,
               p.sectors, p.size // (1024 * 1024),
               "  <- Xenix" if p.is_xenix else ""))
    return 0


def cmd_info(args):
    with open_fs(args) as fs:
        print("image           : %s" % fs.path)
        print("offset          : %d" % fs.offset)
        print("block size      : %d" % BLOCK_SIZE)
        print("s_isize         : %d (first data block)" % fs.s_isize)
        print("s_fsize         : %d blocks (%.1f MiB)"
              % (fs.s_fsize, fs.s_fsize * BLOCK_SIZE / 1048576.0))
        print("i-list          : blocks %d..%d (%d inodes)"
              % (ILIST_START_BLOCK, fs.s_isize - 1, fs.num_inodes))
        root = fs.inode(ROOT_INO)
        print("root inode      : mode %s nlink %d size %d"
              % (mode_string(root.mode), root.nlink, root.size))
    return 0


def cmd_ls(args):
    with open_fs(args) as fs:
        ino = fs.lookup(args.path)
        if ino.is_dir() and not args.recursive:
            entries = [(n, fs.inode(num)) for num, n in
                       sorted(fs.readdir(ino), key=lambda e: e[1])]
            for name, child in entries:
                print("%s %3d %5d %8d  %s" %
                      (mode_string(child.mode), child.nlink, child.num,
                       child.size, name))
        elif ino.is_dir():
            for path, child in fs.walk(ino, args.path):
                print("%s %3d %5d %8d  %s" %
                      (mode_string(child.mode), child.nlink, child.num,
                       child.size, path))
        else:
            print("%s %3d %5d %8d  %s" %
                  (mode_string(ino.mode), ino.nlink, ino.num, ino.size,
                   args.path))
    return 0


def cmd_cat(args):
    with open_fs(args) as fs:
        ino = fs.lookup(args.path)
        if not ino.is_reg():
            raise XenixError("%s is not a regular file" % args.path)
        sys.stdout.buffer.write(fs.read_file(ino))
    return 0


def cmd_extract(args):
    written = skipped = links = 0
    seen = {}                      # inode number -> first path written
    with open_fs(args) as fs:
        top = fs.lookup(args.path)
        if not top.is_dir():
            raise XenixError("%s is not a directory" % args.path)
        os.makedirs(args.dest, exist_ok=True)
        for path, ino in fs.walk(top, args.path):
            rel = os.path.relpath(path, args.path).lstrip("/")
            dest = os.path.join(args.dest, rel)

            if ino.is_dir():
                os.makedirs(dest, exist_ok=True)
                continue

            if ino.is_special():
                skipped += 1
                if args.verbose:
                    print("skip (special %s): %s" % (mode_string(ino.mode),
                                                     path), file=sys.stderr)
                continue

            os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)

            # Reproduce hard links rather than duplicating the data.
            if ino.nlink > 1 and ino.num in seen:
                if os.path.exists(dest):
                    os.unlink(dest)
                os.link(seen[ino.num], dest)
                links += 1
                continue

            data = fs.read_file(ino)
            if ino.is_lnk():
                target = data.split(b"\0")[0].decode("latin-1")
                if os.path.lexists(dest):
                    os.unlink(dest)
                os.symlink(target, dest)
            else:
                with open(dest, "wb") as out:
                    out.write(data)
                os.chmod(dest, stat.S_IMODE(ino.mode))
                if ino.mtime:
                    os.utime(dest, (ino.atime or ino.mtime, ino.mtime))
            seen.setdefault(ino.num, dest)
            written += 1
            if args.verbose:
                print("%s -> %s" % (path, dest), file=sys.stderr)

    print("extracted %d file(s), %d hard link(s), %d special file(s) skipped"
          % (written, links, skipped), file=sys.stderr)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Read SCO Xenix / System V filesystem images.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Without -p/-o a partitioned image uses its sole Xenix "
               "partition and an unpartitioned one is read from offset 0.")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def add_locator(p):
        g = p.add_mutually_exclusive_group()
        g.add_argument("-p", "--partition", type=int, metavar="N",
                       help="read the filesystem in MBR partition N (1-4)")
        g.add_argument("-o", "--offset", type=int, metavar="BYTES",
                       help="read the filesystem at this byte offset")

    p = sub.add_parser("partitions", help="list the MBR partition table")
    p.add_argument("image")
    p.set_defaults(func=cmd_partitions)

    p = sub.add_parser("info", help="show superblock geometry")
    p.add_argument("image")
    add_locator(p)
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("ls", help="list a directory")
    p.add_argument("image")
    p.add_argument("path", nargs="?", default="/")
    p.add_argument("-R", "--recursive", action="store_true")
    add_locator(p)
    p.set_defaults(func=cmd_ls)

    p = sub.add_parser("cat", help="write a file to stdout")
    p.add_argument("image")
    p.add_argument("path")
    add_locator(p)
    p.set_defaults(func=cmd_cat)

    p = sub.add_parser("extract", help="extract a tree to a host directory")
    p.add_argument("image")
    p.add_argument("dest")
    p.add_argument("--path", default="/", help="subtree to extract")
    p.add_argument("-v", "--verbose", action="store_true")
    add_locator(p)
    p.set_defaults(func=cmd_extract)

    args = ap.parse_args(argv)
    try:
        return args.func(args)
    except XenixError as e:
        print("xenixfs: %s" % e, file=sys.stderr)
        return 1
    except BrokenPipeError:
        return 0
    except OSError as e:
        if e.errno == errno.EPIPE:
            return 0
        raise


if __name__ == "__main__":
    sys.exit(main())
