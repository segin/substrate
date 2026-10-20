#!/usr/bin/env python3
"""
mksysvimg.py -- write a small System V filesystem image for the sysv
driver's tests (tests/kern/sysvfs.sh).

  mksysvimg.py <out.img>          Release 2 superblock: s_fsize at offset 2
  mksysvimg.py --svr4 <out.img>   Release 4 superblock: s_fsize at offset 4,
                                  after a pad, and an s_time after 1980
  mksysvimg.py --big <out>        write only /big's contents, for comparison

Layout: 1 KiB blocks (type 2), magic 0xfd187e20 at 512+0x1f8, s_isize = 10
(the inode table is blocks 2..9, 128 inodes; data from block 10),
s_fsize = 512.  Inodes are 64 bytes with 13 three-byte zone numbers.

  /small                 "hello\\n"
  /big                   20 KiB (10 direct blocks + a single-indirect one),
                         byte i = i * 7 & 0xff
  /a/b/c/d/e/target      "deep\\n"
  /many/f0 .. f59        "file N\\n"
  /badino                directory entry naming inode 500 (past the table)
  /badblk                a file whose first block is 9999 (past the volume)
"""
import struct
import sys

BIG = bytes((i * 7) & 0xFF for i in range(20 * 1024))
if sys.argv[1] == "--big":
    with open(sys.argv[2], "wb") as f:
        f.write(BIG)
    sys.exit(0)
SVR4 = sys.argv[1] == "--svr4"
OUT = sys.argv[-1]

BS = 1024
NBLOCKS = 512
ISIZE = 10                      # first data block
INODES = (ISIZE - 2) * (BS // 64)

img = bytearray(NBLOCKS * BS)
next_block = ISIZE
next_ino = 2                    # 1 is the bad-block inode


def alloc_block():
    global next_block
    b = next_block
    next_block += 1
    return b


def zones(blocks):
    """i_addr for a list of data blocks (direct, then single indirect)."""
    addr = list(blocks[:10])
    if len(blocks) > 10:
        ind = alloc_block()
        rest = blocks[10:]
        for i, b in enumerate(rest):
            img[ind * BS + i * 3: ind * BS + i * 3 + 3] = b.to_bytes(3, "little")
        addr.append(ind)
    addr += [0] * (13 - len(addr))
    raw = b"".join(z.to_bytes(3, "little") for z in addr)
    return raw + b"\0"          # 39 bytes + pad = 40


def write_inode(ino, mode, nlink, size, addr40):
    off = 2 * BS + (ino - 1) * 64
    img[off:off + 64] = struct.pack("<HHHHI40sIII", mode, nlink, 0, 0, size,
                                    addr40, 0, 0, 0)


def new_ino():
    global next_ino
    i = next_ino
    next_ino += 1
    return i


def add_file(data, ino=None):
    ino = ino or new_ino()
    blocks = []
    for o in range(0, len(data), BS):
        b = alloc_block()
        chunk = data[o:o + BS]
        img[b * BS:b * BS + len(chunk)] = chunk
        blocks.append(b)
    write_inode(ino, 0o100644, 1, len(data), zones(blocks))
    return ino


def add_dir(entries, ino, parent):
    """entries: list of (name, ino)."""
    ents = [(".", ino), ("..", parent)] + entries
    data = b"".join(struct.pack("<H14s", i, n.encode()) for n, i in ents)
    blocks = []
    for o in range(0, len(data), BS):
        b = alloc_block()
        chunk = data[o:o + BS]
        img[b * BS:b * BS + len(chunk)] = chunk
        blocks.append(b)
    write_inode(ino, 0o040755, 2, len(data), zones(blocks))


ROOT = 2
next_ino = 3
small = add_file(b"hello\n")
big = add_file(BIG)

# a/b/c/d/e/target
chain = [new_ino() for _ in range(5)]
target = add_file(b"deep\n")
names = ["a", "b", "c", "d", "e"]
for k in range(4, -1, -1):
    parent = chain[k - 1] if k else ROOT
    child = [(names[k + 1], chain[k + 1])] if k < 4 else [("target", target)]
    add_dir(child, chain[k], parent)

many = new_ino()
files = [(f"f{i}", add_file(f"file {i}\n".encode())) for i in range(60)]
add_dir(files, many, ROOT)

badblk = new_ino()
write_inode(badblk, 0o100644, 1, 100, zones([9999]))

add_dir([("small", small), ("big", big), ("a", chain[0]), ("many", many),
         ("badino", 500), ("badblk", badblk)], ROOT, ROOT)

assert next_ino <= INODES and next_block <= NBLOCKS

sb = 512
img[sb:sb + 2] = struct.pack("<H", ISIZE)
if SVR4:
    img[sb + 4:sb + 8] = struct.pack("<I", NBLOCKS)
    img[sb + 420:sb + 424] = struct.pack("<I", 1000000000)   # s_time, 2001
else:
    img[sb + 2:sb + 6] = struct.pack("<I", NBLOCKS)
img[sb + 0x1F8:sb + 0x200] = struct.pack("<II", 0xFD187E20, 2)

with open(OUT, "wb") as f:
    f.write(img)
