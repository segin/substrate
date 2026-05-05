#!/usr/bin/env python3

import os
import pty
import subprocess
import sys


def require(cond, msg):
    if not cond:
        raise SystemExit(msg)


def run_non_tty(tty_path, *args):
    return subprocess.run(
        [tty_path, *args],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def run_with_pty(tty_path, *args):
    master_fd, slave_fd = pty.openpty()
    try:
        tty_name = os.ttyname(slave_fd)
        proc = subprocess.run(
            [tty_path, *args],
            stdin=slave_fd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    finally:
        os.close(master_fd)
        os.close(slave_fd)
    return proc, tty_name


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/tty", file=sys.stderr)
        return 2

    tty_path = sys.argv[1]

    proc = run_non_tty(tty_path)
    require(proc.returncode == 1, f"non-tty exit status {proc.returncode}")
    require(proc.stdout == b"not a tty\n",
            f"unexpected non-tty stdout: {proc.stdout!r}")
    require(proc.stderr == b"",
            f"unexpected non-tty stderr: {proc.stderr!r}")

    proc = run_non_tty(tty_path, "-s")
    require(proc.returncode == 1, f"silent non-tty exit status {proc.returncode}")
    require(proc.stdout == b"",
            f"unexpected silent non-tty stdout: {proc.stdout!r}")
    require(proc.stderr == b"",
            f"unexpected silent non-tty stderr: {proc.stderr!r}")

    proc = run_non_tty(tty_path, "-x")
    require(proc.returncode == 2, f"bad-option exit status {proc.returncode}")
    require(proc.stdout == b"",
            f"unexpected bad-option stdout: {proc.stdout!r}")
    require(proc.stderr.startswith(b"usage: "),
            f"unexpected bad-option stderr: {proc.stderr!r}")

    proc, tty_name = run_with_pty(tty_path)
    require(proc.returncode == 0, f"pty exit status {proc.returncode}")
    require(proc.stdout == os.fsencode(tty_name) + b"\n",
            f"unexpected pty stdout: {proc.stdout!r}")
    require(proc.stderr == b"",
            f"unexpected pty stderr: {proc.stderr!r}")

    proc, _ = run_with_pty(tty_path, "-s")
    require(proc.returncode == 0, f"silent pty exit status {proc.returncode}")
    require(proc.stdout == b"",
            f"unexpected silent pty stdout: {proc.stdout!r}")
    require(proc.stderr == b"",
            f"unexpected silent pty stderr: {proc.stderr!r}")

    print("tty test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
