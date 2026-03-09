#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path

ELKS_COMBID = 0x04100301
ELKS_HDRLEN = 0x20


def main() -> int:
    parser = argparse.ArgumentParser(description="Wrap a flat 16-bit binary in an ELKS Minix a.out header.")
    parser.add_argument("--text", required=True, help="Flat binary payload for the text segment")
    parser.add_argument("--output", required=True, help="Output ELKS executable")
    parser.add_argument("--stack", type=int, default=4096, help="Initial minimum stack reservation")
    parser.add_argument("--heap", type=int, default=0, help="Initial heap reservation (0 uses ELKS default)")
    args = parser.parse_args()

    text = Path(args.text).read_bytes()
    if len(text) > 0xFFFF:
        raise SystemExit("text segment exceeds 16-bit ELKS limit")

    hdr = struct.pack(
        "<IBBHIII IHHI".replace(" ", ""),
        ELKS_COMBID,
        ELKS_HDRLEN,
        0,
        1,
        len(text),
        0,
        0,
        0,
        args.heap & 0xFFFF,
        args.stack & 0xFFFF,
        0,
    )
    Path(args.output).write_bytes(hdr + text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
