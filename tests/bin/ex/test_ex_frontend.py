#!/usr/bin/env python3

import os
import pty
import select
import signal
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


def run_ex_pty(ex_path, argv, initial_timeout=0.4, command=b"q!\n", final_timeout=0.2,
               argv0=None, cwd=None):
    pid, master_fd = pty.fork()
    if pid == 0:
        if cwd is not None:
            os.chdir(cwd)
        os.execv(ex_path, [argv0 or ex_path] + argv)

    initial = read_some(master_fd, initial_timeout)
    if command is not None:
        os.write(master_fd, command)
    final = read_some(master_fd, final_timeout)
    _, status = os.waitpid(pid, 0)
    os.close(master_fd)
    return os.waitstatus_to_exitcode(status), initial.decode("latin1", "replace"), \
        (initial + final).decode("latin1", "replace")


def interrupt_ex_session(ex_path, path, command):
    pid, master_fd = pty.fork()
    if pid == 0:
        os.execv(ex_path, [ex_path, path])

    output = read_some(master_fd, 0.4)
    os.write(master_fd, command)
    output += read_some(master_fd, 0.3)
    os.kill(pid, signal.SIGTERM)
    output += read_some(master_fd, 0.2)
    _, status = os.waitpid(pid, 0)
    os.close(master_fd)
    return os.waitstatus_to_exitcode(status), output.decode("latin1", "replace")


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

    with tempfile.TemporaryDirectory(prefix="exvi-") as temp_dir:
        target = os.path.join(temp_dir, "target.txt")
        with open(target, "w", encoding="utf-8") as f:
            f.write("alpha\nbeta\n")
        with open(os.path.join(temp_dir, "tags"), "w", encoding="utf-8") as f:
            f.write("two\ttarget.txt\t2\n")

        proc = subprocess.run([ex_path, "-s", "-t", "two"], input=b"1p\nq!\n",
                              cwd=temp_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(proc.returncode == 0, f"ex -t batch status mismatch: {proc.returncode}")
        require(proc.stdout.decode("latin1", "replace") == "beta\nalpha\n",
                f"ex -t batch stdout mismatch: {proc.stdout!r}")
        require(proc.stderr == b"", f"ex -t batch stderr mismatch: {proc.stderr!r}")

        exit_code, initial, decoded = run_ex_pty(
            ex_path,
            ["-v", "-t", "two"],
            command=b":q!\r",
            final_timeout=0.4,
            cwd=temp_dir,
        )
        require(exit_code == 0, f"ex -v -t status mismatch: {exit_code}")
        require("line 2/2" in decoded, f"ex -v -t missing tag target status: {decoded!r}")

    proc = subprocess.run([ex_path, "-r"], input=b"",
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(proc.returncode == 1, f"ex -r missing-file status mismatch: {proc.returncode}")
    require(proc.stdout == b"", f"ex -r missing-file stdout mismatch: {proc.stdout!r}")
    require(proc.stderr.decode("latin1", "replace") == "ex: -r requires a file operand\n",
            f"ex -r missing-file stderr mismatch: {proc.stderr!r}")

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("one\n")
        path = f.name
    try:
        with open(path + ".recover", "w", encoding="utf-8") as f2:
            f2.write("RECOVERED\n")
        proc = subprocess.run([ex_path, "-r", path], input=b"1p\nq!\n",
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(proc.returncode == 0, f"ex -r status mismatch: {proc.returncode}")
        decoded = proc.stdout.decode("latin1", "replace")
        require(f"\"{path}\" recovered, 1 lines" in decoded,
                f"ex -r missing recovered banner: {decoded!r}")
        require("RECOVERED\n" in decoded, f"ex -r missing recovered line print: {decoded!r}")
        require(proc.stderr == b"", f"ex -r stderr mismatch: {proc.stderr!r}")
        with open(path, "r", encoding="utf-8") as f2:
            saved = f2.read()
        require(saved == "one\n", f"ex -r unexpectedly modified file: {saved!r}")
    finally:
        if os.path.exists(path + ".recover"):
            os.unlink(path + ".recover")
        os.unlink(path)

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("one\n")
        path = f.name
    try:
        exit_code, decoded = interrupt_ex_session(
            ex_path,
            path,
            b"1change\nINTERRUPTED\n.\n",
        )
        require(exit_code == 1, f"interrupted ex status mismatch: {exit_code}")
        require(os.path.exists(path + ".recover"),
                "interrupted ex did not create a recover file")
        with open(path + ".recover", "r", encoding="utf-8") as f2:
            recovered = f2.read()
        require(recovered == "INTERRUPTED\n",
                f"interrupted ex recover contents mismatch: {recovered!r}")

        proc = subprocess.run([ex_path, "-r", path], input=b"1p\nq!\n",
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(proc.returncode == 0, f"interrupted ex -r status mismatch: {proc.returncode}")
        recover_output = proc.stdout.decode("latin1", "replace")
        require("INTERRUPTED\n" in recover_output,
                f"interrupted ex -r missing recovered content: {recover_output!r}")
    finally:
        if os.path.exists(path + ".recover"):
            os.unlink(path + ".recover")
        os.unlink(path)

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("one")
        path = f.name
    try:
        exit_code, decoded = interrupt_ex_session(
            ex_path,
            path,
            b"1s/o/O/\n",
        )
        require(exit_code == 1, f"interrupted no-eol ex status mismatch: {exit_code}")
        require(os.path.exists(path + ".recover"),
                "interrupted no-eol ex did not create a recover file")
        with open(path + ".recover", "r", encoding="utf-8") as f2:
            recovered = f2.read()
        require(recovered == "One",
                f"interrupted no-eol ex recover contents mismatch: {recovered!r}")
    finally:
        if os.path.exists(path + ".recover"):
            os.unlink(path + ".recover")
        os.unlink(path)

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("one\n")
        path = f.name
    try:
        with open(path + ".recover", "w", encoding="utf-8") as f2:
            f2.write("STALE\n")
        proc = subprocess.run([ex_path, "-s", path], input=b"wq\n",
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(proc.returncode == 0, f"ex stale-recover cleanup status mismatch: {proc.returncode}")
        require(not os.path.exists(path + ".recover"),
                "ex successful write did not remove stale recover file")
    finally:
        if os.path.exists(path + ".recover"):
            os.unlink(path + ".recover")
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

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("alpha\n")
        path = f.name
    try:
        exit_code, initial, decoded = run_ex_pty(
            ex_path,
            ["-S", path],
            command=b"!true\nq!\n",
            final_timeout=0.4,
        )
        require(exit_code == 0, f"secure ex shell escape exited with status {exit_code}")
        require(initial == ":", f"secure ex shell escape missing prompt: {initial!r}")
        require("Shell commands not allowed in secure mode" in decoded,
                "secure ex shell escape missing diagnostic")
        with open(path, "r", encoding="utf-8") as f2:
            saved = f2.read()
        require(saved == "alpha\n", f"secure ex shell escape modified file: {saved!r}")
    finally:
        os.unlink(path)

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("alpha\n")
        path = f.name
    try:
        exit_code, initial, decoded = run_ex_pty(
            ex_path,
            ["-S", path],
            command=b"r !printf 'A\\n'\nq!\n",
            final_timeout=0.4,
        )
        require(exit_code == 0, f"secure ex shell read exited with status {exit_code}")
        require(initial == ":", f"secure ex shell read missing prompt: {initial!r}")
        require("Shell commands not allowed in secure mode" in decoded,
                "secure ex shell read missing diagnostic")
        with open(path, "r", encoding="utf-8") as f2:
            saved = f2.read()
        require(saved == "alpha\n", f"secure ex shell read modified file: {saved!r}")
    finally:
        os.unlink(path)

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("alpha\n")
        path = f.name
    try:
        exit_code, initial, decoded = run_ex_pty(
            ex_path,
            ["-S", path],
            command=b"w !cat\nq!\n",
            final_timeout=0.4,
        )
        require(exit_code == 0, f"secure ex shell write exited with status {exit_code}")
        require(initial == ":", f"secure ex shell write missing prompt: {initial!r}")
        require("Shell commands not allowed in secure mode" in decoded,
                "secure ex shell write missing diagnostic")
        with open(path, "r", encoding="utf-8") as f2:
            saved = f2.read()
        require(saved == "alpha\n", f"secure ex shell write modified file: {saved!r}")
    finally:
        os.unlink(path)

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("alpha\n")
        path = f.name
    try:
        exit_code, initial, decoded = run_ex_pty(
            ex_path,
            [path],
            command=b"!true\nq!\n",
            final_timeout=0.4,
            argv0="rex",
        )
        require(exit_code == 0, f"restricted ex shell escape exited with status {exit_code}")
        require(initial == ":", f"restricted ex shell escape missing prompt: {initial!r}")
        require("Shell commands not allowed in restricted mode" in decoded,
                "restricted ex shell escape missing diagnostic")
        with open(path, "r", encoding="utf-8") as f2:
            saved = f2.read()
        require(saved == "alpha\n", f"restricted ex shell escape modified file: {saved!r}")
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
