#!/usr/bin/env python3
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path


def run_cmd(bin_path, args, stdin_data=b"", env=None):
    proc = subprocess.run(
        [bin_path] + args,
        input=stdin_data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        check=False,
    )
    return proc.returncode, proc.stdout, proc.stderr


def expect(cond, message):
    if not cond:
        raise AssertionError(message)


def expect_eq(actual, expected, message):
    if actual != expected:
        raise AssertionError(
            f"{message}\nexpected={expected!r}\nactual={actual!r}"
        )


def vis_expected(data, show_nonprint=False, show_tabs=False, show_ends=False):
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


def main():
    if len(sys.argv) != 2:
        print("usage: test_cat_unit.py /path/to/cat", file=sys.stderr)
        return 2

    bin_path = sys.argv[1]
    expect(Path(bin_path).is_file(), f"missing binary: {bin_path}")

    with tempfile.TemporaryDirectory(prefix="cat-unit-") as tmpdir:
        td = Path(tmpdir)

        plain = td / "plain.txt"
        plain.write_bytes(b"alpha\n\nbeta\n")

        tabs = td / "tabs.bin"
        tabs.write_bytes(b"\tA\t\n")

        ctrl = td / "ctrl.bin"
        ctrl_data = bytes([0x00, 0x09, 0x0A, 0x1F, 0x20, 0x7F, 0x80, 0x81, 0xFF])
        ctrl.write_bytes(ctrl_data)

        newlines = td / "newlines.txt"
        newlines.write_bytes(b"\n\n\n")

        empty = td / "empty.txt"
        empty.write_bytes(b"")

        combo = td / "combo.bin"
        combo.write_bytes(b"\nA\t\x01\n\nB\n")

        good = td / "good.txt"
        good.write_bytes(b"GOOD\n")

        huge = td / "huge.bin"
        rng = random.Random(20260227)
        huge_data = bytes(rng.getrandbits(8) for _ in range(2 * 1024 * 1024))
        huge.write_bytes(huge_data)

        rc, out, err = run_cmd(bin_path, [str(plain)])
        expect_eq(rc, 0, "basic cat exit")
        expect_eq(out, plain.read_bytes(), "basic cat output")
        expect_eq(err, b"", "basic cat stderr")

        rc, out, _ = run_cmd(bin_path, ["-n", str(plain)])
        expect_eq(rc, 0, "-n exit")
        expect_eq(out, b"     1\talpha\n     2\t\n     3\tbeta\n", "-n output")

        rc, out, _ = run_cmd(bin_path, ["-b", str(plain)])
        expect_eq(rc, 0, "-b exit")
        expect_eq(out, b"     1\talpha\n\n     2\tbeta\n", "-b output")

        rc, out, _ = run_cmd(bin_path, ["-s", str(newlines)])
        expect_eq(rc, 0, "-s exit")
        expect_eq(out, b"\n", "-s output")

        rc, out, _ = run_cmd(bin_path, ["-e", str(plain)])
        expect_eq(rc, 0, "-e exit")
        expect_eq(out, b"alpha$\n$\nbeta$\n", "-e output")

        rc, out, _ = run_cmd(bin_path, ["-t", str(tabs)])
        expect_eq(rc, 0, "-t exit")
        expect_eq(out, b"^IA^I\n", "-t output")

        rc, out, _ = run_cmd(bin_path, ["-v", str(ctrl)])
        expect_eq(rc, 0, "-v exit")
        expect_eq(out, vis_expected(ctrl_data, show_nonprint=True), "-v output")

        rc, out, _ = run_cmd(bin_path, ["-t", str(ctrl)])
        expect_eq(rc, 0, "-t implies -v exit")
        expect_eq(
            out,
            vis_expected(ctrl_data, show_nonprint=True, show_tabs=True),
            "-t implies -v output",
        )

        rc, out, _ = run_cmd(bin_path, ["-e", str(ctrl)])
        expect_eq(rc, 0, "-e implies -v exit")
        expect_eq(
            out,
            vis_expected(ctrl_data, show_nonprint=True, show_ends=True),
            "-e implies -v output",
        )

        rc, out, _ = run_cmd(bin_path, ["-b", "-e", "-s", "-t", str(combo)])
        expect_eq(rc, 0, "combined flags exit")
        expect_eq(out, b"$\n     1\tA^I^A$\n$\n     2\tB$\n", "combined flags output")

        rc, out, err = run_cmd(bin_path, ["-B", "32", str(huge)])
        expect_eq(rc, 0, "-B decimal exit")
        expect_eq(out, huge_data, "-B decimal output")
        expect_eq(err, b"", "-B decimal stderr")

        rc, out, err = run_cmd(bin_path, ["-B", "0x100", str(huge)])
        expect_eq(rc, 0, "-B hex exit")
        expect_eq(out, huge_data, "-B hex output")
        expect_eq(err, b"", "-B hex stderr")

        rc, _, _ = run_cmd(bin_path, ["-B", "0", str(plain)])
        expect(rc != 0, "-B 0 should fail")

        rc, out, _ = run_cmd(bin_path, [str(empty)])
        expect_eq(rc, 0, "empty file exit")
        expect_eq(out, b"", "empty file output")

        rc, out, _ = run_cmd(bin_path, [str(newlines)])
        expect_eq(rc, 0, "only-newlines exit")
        expect_eq(out, b"\n\n\n", "only-newlines output")

        missing = td / "missing.txt"
        rc, out, err = run_cmd(bin_path, [str(missing), str(good)])
        expect(rc != 0, "missing file should set failure")
        expect_eq(out, good.read_bytes(), "good file should still be processed")
        expect(str(missing).encode() in err, "missing filename should appear in stderr")

    print("unit tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1)
