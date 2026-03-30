#!/usr/bin/env python3

import os
import pty
import select
import subprocess
import sys
import tempfile
import time


def read_some(fd, timeout):
    out = bytearray()
    end = time.time() + timeout
    while time.time() < end:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        out.extend(chunk)
    return bytes(out)


def require(cond, msg):
    if not cond:
        print(f"FAIL: {msg}", file=sys.stderr)
        raise SystemExit(1)


def run_ex_pty(ex_path, argv, initial_timeout=0.4, command=b"q!\n", final_timeout=0.2):
    pid, master_fd = pty.fork()
    if pid == 0:
        os.execv(ex_path, [ex_path] + argv)

    initial = read_some(master_fd, initial_timeout)
    if command is not None:
        os.write(master_fd, command)
    final = read_some(master_fd, final_timeout)
    _, status = os.waitpid(pid, 0)
    os.close(master_fd)
    return os.waitstatus_to_exitcode(status), initial.decode("latin1", "replace"), \
        (initial + final).decode("latin1", "replace")


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/ex", file=sys.stderr)
        return 2

    ex_path = sys.argv[1]

    exit_code, initial, decoded = run_ex_pty(ex_path, [])
    require(exit_code == 0, f"interactive ex exited with status {exit_code}")
    require(initial == ":", f"missing initial ex prompt: {initial!r}")
    require(decoded.startswith(":q!"), f"unexpected interactive ex transcript: {decoded!r}")

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("one\n")
        path = f.name
    try:
        exit_code, initial, decoded = run_ex_pty(ex_path, ["-s", path])
        require(exit_code == 0, f"batch ex exited with status {exit_code}")
        require(initial == "", f"batch ex should not print a prompt: {decoded!r}")
    finally:
        os.unlink(path)

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("one\n")
        path = f.name
    try:
        proc = subprocess.run([ex_path, "-v", path], input=b"",
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(proc.returncode == 1, f"ex -v non-tty status mismatch: {proc.returncode}")
        require(proc.stderr.decode("latin1", "replace") == "vi: visual mode requires a terminal.\n",
                f"ex -v non-tty stderr mismatch: {proc.stderr!r}")
    finally:
        os.unlink(path)

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("alpha\n")
        path = f.name
    try:
        exit_code, initial, decoded = run_ex_pty(
            ex_path,
            ["-R", path],
            command=b"1change\nblocked\n.\nw\nq!\n",
            final_timeout=0.4,
        )
        require(exit_code == 0, f"readonly ex exited with status {exit_code}")
        require(initial == ":", f"readonly ex missing prompt: {initial!r}")
        require("File is read only (add ! to override)" in decoded,
                "readonly ex missing blocked-write diagnostic")
        with open(path, "r", encoding="utf-8") as f2:
            saved = f2.read()
        require(saved == "alpha\n", f"readonly ex unexpectedly modified file: {saved!r}")
    finally:
        os.unlink(path)

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("alpha\n")
        path = f.name
    try:
        exit_code, initial, decoded = run_ex_pty(
            ex_path,
            ["-R", path],
            command=b"1change\nforced\n.\nw!\nq!\n",
            final_timeout=0.4,
        )
        require(exit_code == 0, f"readonly force-write ex exited with status {exit_code}")
        require(initial == ":", f"readonly force-write ex missing prompt: {initial!r}")
        with open(path, "r", encoding="utf-8") as f2:
            saved = f2.read()
        require(saved == "forced\n",
                f"readonly force-write ex saved wrong file contents: {saved!r}")
    finally:
        os.unlink(path)

    proc = subprocess.run([ex_path], input=b"version\nq!\n",
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(proc.returncode == 0, f"ex version status mismatch: {proc.returncode}")
    require(proc.stdout.decode("latin1", "replace") == "Substrate vi v0.1\n",
            f"ex version stdout mismatch: {proc.stdout!r}")
    require(proc.stderr == b"", f"ex version stderr mismatch: {proc.stderr!r}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
