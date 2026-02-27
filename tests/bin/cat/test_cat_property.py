#!/usr/bin/env python3
import random
import subprocess
import sys
from pathlib import Path


def expect(cond, msg):
    if not cond:
        raise AssertionError(msg)


def expected_visual(data, show_nonprint=False, show_tabs=False, show_ends=False):
    out = bytearray()

    def vis7(b):
        if b < 0x20:
            out.extend((ord("^"), ord("@") + b))
        elif b == 0x7F:
            out.extend(b"^?")
        else:
            out.append(b)

    for b in data:
        if b == 0x0A:
            if show_ends:
                out.append(ord("$"))
            out.append(0x0A)
            continue
        if b == 0x09:
            if show_tabs:
                out.extend(b"^I")
            else:
                out.append(0x09)
            continue
        if not show_nonprint:
            out.append(b)
            continue
        if b & 0x80:
            out.extend(b"M-")
            vis7(b & 0x7F)
            continue
        vis7(b)

    return bytes(out)


def run_case(bin_path, args, payload):
    proc = subprocess.run(
        [bin_path] + args,
        input=payload,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    return proc.returncode, proc.stdout, proc.stderr


def main():
    if len(sys.argv) != 2:
        print("usage: test_cat_property.py /path/to/cat", file=sys.stderr)
        return 2

    bin_path = sys.argv[1]
    expect(Path(bin_path).is_file(), f"missing binary: {bin_path}")

    rng = random.Random(424242)
    combos = [
        (["-v"], dict(show_nonprint=True, show_tabs=False, show_ends=False)),
        (["-v", "-t"], dict(show_nonprint=True, show_tabs=True, show_ends=False)),
        (["-v", "-e"], dict(show_nonprint=True, show_tabs=False, show_ends=True)),
    ]

    for args, cfg in combos:
        for i in range(300):
            length = rng.randint(0, 512)
            payload = bytes(rng.getrandbits(8) for _ in range(length))
            expected = expected_visual(payload, **cfg)

            rc, out, err = run_case(bin_path, args, payload)
            expect(rc == 0, f"property case exit failed for {args}, iter={i}: {err!r}")
            expect(
                out == expected,
                f"property mismatch args={args} iter={i} expected={expected[:80]!r} got={out[:80]!r}",
            )

    print("property tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1)
