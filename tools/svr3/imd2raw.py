#!/usr/bin/env python3
"""Convert an ImageDisk (.IMD) floppy image to a raw sector image.

REQUIRES EXTERNAL MEDIA: this reads distribution floppy images that are
not in the repository.  See build-image.sh in this directory for what to
obtain and how to pass it in.

ImageDisk is the format bitsavers archives floppies in.  An .IMD file is
an ASCII comment ended by 0x1A, then one record per track:

    mode, cylinder, head, sector count, sector size code
    sector numbering map         (one byte per sector)
    cylinder map                 (if head bit 7)
    head map                     (if head bit 6)
    per sector: a type byte, then
        0        unavailable -- no data
        1 3 5 7  the sector's bytes
        2 4 6 8  one byte, repeated to fill the sector

The sector size is 128 << code.  Odd types above 1 are deleted or errored
sectors whose data was still read; they are written as they are.

The output is the sectors in cylinder, head, sector-number order, each
missing one filled with zeroes.

usage: imd2raw.py INPUT.IMD OUTPUT.img
"""
import sys


def decode(data):
    end = data.find(b"\x1a")
    if not data.startswith(b"IMD ") or end < 0:
        raise SystemExit("imd2raw: not an ImageDisk file")
    pos = end + 1
    tracks = {}
    size = None
    while pos < len(data):
        _mode, cyl, head, count, code = data[pos:pos + 5]
        pos += 5
        if code > 6:
            raise SystemExit("imd2raw: unsupported sector size code %d" % code)
        size = 128 << code
        numbers = data[pos:pos + count]
        pos += count
        if head & 0x80:
            pos += count
        if head & 0x40:
            pos += count
        sectors = {}
        for n in numbers:
            kind = data[pos]
            pos += 1
            if kind == 0:
                continue
            if kind > 8:
                raise SystemExit("imd2raw: bad sector type %d" % kind)
            if kind & 1:
                sectors[n] = data[pos:pos + size]
                pos += size
            else:
                sectors[n] = bytes([data[pos]]) * size
                pos += 1
        tracks[(cyl, head & 1)] = (sectors, sorted(numbers))
    return tracks, size


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    with open(argv[1], "rb") as fh:
        tracks, size = decode(fh.read())
    if not tracks:
        raise SystemExit("imd2raw: no tracks")
    cyls = max(c for c, _ in tracks) + 1
    heads = max(h for _, h in tracks) + 1
    # Every track is laid out with the sector numbers the fullest one has.
    layout = max((nums for _, nums in tracks.values()), key=len)
    missing = 0
    with open(argv[2], "wb") as out:
        for cyl in range(cyls):
            for head in range(heads):
                sectors = tracks.get((cyl, head), ({}, []))[0]
                for n in layout:
                    if n in sectors:
                        out.write(sectors[n])
                    else:
                        out.write(bytes(size))
                        missing += 1
    print("%d cylinders, %d heads, %d sectors of %d bytes; %d missing" %
          (cyls, heads, len(layout), size, missing))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
