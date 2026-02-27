#!/usr/bin/env python3
import os
import pty
import select
import subprocess
import sys
import tempfile
import threading
import time
import errno
from pathlib import Path


def expect(cond, msg):
    if not cond:
        raise AssertionError(msg)


def expect_eq(actual, expected, msg):
    if actual != expected:
        raise AssertionError(f"{msg}\nexpected={expected!r}\nactual={actual!r}")


def run_capture(bin_path, args, stdin_data=b"", timeout=5):
    proc = subprocess.run(
        [bin_path] + args,
        input=stdin_data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=timeout,
    )
    return proc.returncode, proc.stdout, proc.stderr


def run_pty_stdout(bin_path, args):
    master_fd, slave_fd = pty.openpty()
    proc = subprocess.Popen(
        [bin_path] + args,
        stdin=subprocess.DEVNULL,
        stdout=slave_fd,
        stderr=subprocess.PIPE,
        close_fds=True,
    )
    os.close(slave_fd)

    output = bytearray()
    while True:
        rlist, _, _ = select.select([master_fd], [], [], 0.2)
        if rlist:
            try:
                chunk = os.read(master_fd, 4096)
            except OSError as exc:
                if exc.errno == errno.EIO:
                    break
                raise
            if chunk:
                output.extend(chunk)
            else:
                break
        if proc.poll() is not None and not rlist:
            break

    os.close(master_fd)
    stderr = proc.stderr.read() if proc.stderr is not None else b""
    rc = proc.wait(timeout=5)
    return rc, bytes(output), stderr


def main():
    if len(sys.argv) != 2:
        print("usage: test_cat_integration.py /path/to/cat", file=sys.stderr)
        return 2

    bin_path = sys.argv[1]
    expect(Path(bin_path).is_file(), f"missing binary: {bin_path}")

    with tempfile.TemporaryDirectory(prefix="cat-int-") as tmpdir:
        td = Path(tmpdir)

        f1 = td / "f1.txt"
        f2 = td / "f2.txt"
        f1.write_bytes(b"file-1\n")
        f2.write_bytes(b"file-2\n")

        rc, out, err = run_capture(bin_path, [], stdin_data=b"from-pipe\n")
        expect_eq(rc, 0, "stdin from pipe exit")
        expect_eq(out, b"from-pipe\n", "stdin from pipe output")
        expect_eq(err, b"", "stdin from pipe stderr")

        rc, out, _ = run_capture(bin_path, [str(f1), "-", str(f2)], stdin_data=b"stdin-segment\n")
        expect_eq(rc, 0, "mixed files and stdin exit")
        expect_eq(out, b"file-1\nstdin-segment\nfile-2\n", "mixed files and stdin output")

        rc, out, _ = run_capture(bin_path, ["--"], stdin_data=b"stdin-via-double-dash\n")
        expect_eq(rc, 0, "double-dash stdin exit")
        expect_eq(out, b"stdin-via-double-dash\n", "double-dash stdin output")

        out_file = td / "out.txt"
        with out_file.open("wb") as fp:
            proc = subprocess.run(
                [bin_path, str(f1), str(f2)],
                stdout=fp,
                stderr=subprocess.PIPE,
                check=False,
            )
        expect_eq(proc.returncode, 0, "stdout regular file exit")
        expect_eq(out_file.read_bytes(), b"file-1\nfile-2\n", "stdout regular file output")

        rc, out, err = run_pty_stdout(bin_path, [str(f1)])
        expect_eq(rc, 0, "stdout pty exit")
        expect_eq(out, b"file-1\r\n", "stdout pty output")
        expect_eq(err, b"", "stdout pty stderr")

        rc, out, err = run_capture(bin_path, ["-f", str(f1)])
        expect_eq(rc, 0, "-f regular file exit")
        expect_eq(out, b"file-1\n", "-f regular file output")
        expect_eq(err, b"", "-f regular file stderr")

        rc, out, _ = run_capture(bin_path, ["-f", "/dev/null"])
        expect_eq(rc, 0, "-f /dev/null exit")
        expect_eq(out, b"", "-f /dev/null output")

        fifo = td / "pipe.fifo"
        os.mkfifo(fifo)

        def fifo_writer():
            with fifo.open("wb", buffering=0) as fp:
                fp.write(b"fifo-data\n")

        writer = threading.Thread(target=fifo_writer, daemon=True)
        writer.start()
        rc, out, err = run_capture(bin_path, ["-f", str(fifo)], timeout=8)
        writer.join(timeout=2)

        expect_eq(rc, 0, "-f fifo exit")
        expect_eq(out, b"fifo-data\n", "-f fifo output")
        expect_eq(err, b"", "-f fifo stderr")

    print("integration tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1)
