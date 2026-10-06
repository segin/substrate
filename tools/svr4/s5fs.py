#!/usr/bin/env python3
"""Read a UNIX System V "s5" filesystem image (list, cat, extract).

REQUIRES EXTERNAL MEDIA: this reads distribution floppy images that are
not in the repository.  See build-image.sh in this directory for what to
obtain and how to pass it in.

The SVR4/386 boot floppies are s5 filesystems, and they are the only place
the release ships /sbin/sh and /usr/lib/libc.so.1.

On-disk format, as found on the Intel SVR4.0 v2 floppies:

    byte 512       superblock; s_magic 0xfd187e20 at +0x1f8, s_type at
                   +0x1fc (1 = 512-byte blocks, 2 = 1024, 3 = 2048)
    block 2..      inode list, 64 bytes each; the root is inode 2
    block s_isize  first data block

    inode: +0 u16 mode, +2 i16 nlink, +4 u16 uid, +6 u16 gid, +8 u32 size,
           +12 thirteen 3-byte little-endian block numbers (10 direct,
           then single, double and triple indirect; indirect blocks hold
           u32 numbers), then atime, mtime, ctime

    directory: 16-byte records, u16 inode and a 14-byte NUL-padded name

usage: s5fs.py ls      IMAGE [PATH] [-o BYTES]
       s5fs.py cat     IMAGE PATH   [-o BYTES]
       s5fs.py extract IMAGE DEST   [-o BYTES]

-o gives the byte offset of the filesystem in the image; without it the
image is searched for the superblock magic.  extract writes regular files,
directories and symbolic links, records modes in DEST/.modes ("octal-mode
path" per line) and skips device nodes.
"""
import os
import struct
import sys

MAGIC = 0xFD187E20
S_IFMT, S_IFDIR, S_IFREG, S_IFLNK = 0o170000, 0o040000, 0o100000, 0o120000


class S5:
    def __init__(self, data, offset=None):
        if offset is None:
            offset = self.find(data)
        self.d = data
        self.base = offset
        magic, kind = struct.unpack_from("<II", data, offset + 512 + 0x1F8)
        if magic != MAGIC or kind not in (1, 2, 3):
            raise SystemExit("s5fs: no s5 superblock at offset %d" % offset)
        self.bsize = 512 << (kind - 1)
        self.isize = struct.unpack_from("<H", data, offset + 512)[0]

    @staticmethod
    def find(data):
        pat = struct.pack("<I", MAGIC)
        pos = data.find(pat)
        while pos >= 0:
            start = pos - 0x1F8 - 512
            if start >= 0 and start % 512 == 0:
                return start
            pos = data.find(pat, pos + 1)
        raise SystemExit("s5fs: no s5 filesystem found")

    def block(self, n):
        o = self.base + n * self.bsize
        return self.d[o:o + self.bsize]

    def inode(self, n):
        o = self.base + 2 * self.bsize + (n - 1) * 64
        mode, nlink, uid, gid, size = struct.unpack_from("<HhHHI", self.d, o)
        addrs = [int.from_bytes(self.d[o + 12 + i * 3:o + 15 + i * 3],
                                "little") for i in range(13)]
        return mode, nlink, uid, gid, size, addrs

    def _indirect(self, blk, depth):
        if blk == 0:
            return
        nums = struct.unpack("<%dI" % (self.bsize // 4), self.block(blk))
        for n in nums:
            if depth == 1:
                yield n
            else:
                yield from self._indirect(n, depth - 1)

    def read(self, n):
        mode, _, _, _, size, addrs = self.inode(n)
        out = bytearray()

        def blocks():
            yield from addrs[:10]
            for depth, a in enumerate(addrs[10:], 1):
                yield from self._indirect(a, depth)

        for b in blocks():
            if len(out) >= size:
                break
            out += self.block(b) if b else bytes(self.bsize)
        return bytes(out[:size])

    def listdir(self, n):
        raw = self.read(n)
        for i in range(0, len(raw) - 15, 16):
            ino = struct.unpack_from("<H", raw, i)[0]
            name = raw[i + 2:i + 16].split(b"\0")[0].decode("latin-1")
            if ino and name not in (".", ".."):
                yield name, ino

    def lookup(self, path):
        n = 2
        for part in [p for p in path.split("/") if p]:
            for name, ino in self.listdir(n):
                if name == part:
                    n = ino
                    break
            else:
                raise SystemExit("s5fs: %s: not found" % path)
        return n

    def walk(self, n=2, prefix=""):
        for name, ino in sorted(self.listdir(n)):
            path = prefix + "/" + name
            yield path, ino
            if self.inode(ino)[0] & S_IFMT == S_IFDIR:
                yield from self.walk(ino, path)


def main(argv):
    args = argv[1:]
    offset = None
    if "-o" in args:
        i = args.index("-o")
        offset = int(args[i + 1])
        del args[i:i + 2]
    if len(args) < 2 or args[0] not in ("ls", "cat", "extract"):
        sys.stderr.write(__doc__)
        return 2
    with open(args[1], "rb") as fh:
        fs = S5(fh.read(), offset)

    if args[0] == "ls":
        top = args[2] if len(args) > 2 else "/"
        for name, ino in sorted(fs.listdir(fs.lookup(top))):
            mode, nlink, uid, gid, size, _ = fs.inode(ino)
            print("%06o %3d %5d %5d %8d  %s" %
                  (mode, nlink, uid, gid, size, name))
        return 0
    if args[0] == "cat":
        if len(args) != 3:
            sys.stderr.write(__doc__)
            return 2
        sys.stdout.buffer.write(fs.read(fs.lookup(args[2])))
        return 0

    if len(args) != 3:
        sys.stderr.write(__doc__)
        return 2
    dest = args[2]
    os.makedirs(dest, exist_ok=True)
    files = skipped = 0
    with open(os.path.join(dest, ".modes"), "a") as modes:
        for path, ino in fs.walk():
            mode = fs.inode(ino)[0]
            rel = path.lstrip("/")
            target = os.path.join(dest, rel)
            kind = mode & S_IFMT
            if kind == S_IFDIR:
                os.makedirs(target, exist_ok=True)
            elif kind == S_IFREG:
                os.makedirs(os.path.dirname(target), exist_ok=True)
                if os.path.lexists(target):
                    os.chmod(target, 0o600)
                with open(target, "wb") as out:
                    out.write(fs.read(ino))
                files += 1
            elif kind == S_IFLNK:
                if os.path.lexists(target):
                    os.unlink(target)
                os.symlink(fs.read(ino).decode("latin-1"), target)
                continue
            else:
                skipped += 1
                continue
            modes.write("%o %s\n" % (mode & 0o7777, rel))
    print("%d file(s), %d special file(s) skipped" % (files, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
