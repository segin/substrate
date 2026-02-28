#!/usr/bin/env python3
import errno
import fcntl
import os
import random
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def expect(cond, msg):
    if not cond:
        raise AssertionError(msg)


def expect_eq(actual, expected, msg):
    if actual != expected:
        raise AssertionError(f"{msg}\nexpected={expected!r}\nactual={actual!r}")


def run_cmd(bin_path, args, stdin_data=b"", env=None, timeout=8):
    proc = subprocess.run(
        [bin_path] + args,
        input=stdin_data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        check=False,
        timeout=timeout,
    )
    return proc.returncode, proc.stdout, proc.stderr


def main():
    if len(sys.argv) != 3:
        print("usage: test_cat_regression.py /path/to/cat /path/to/cat_test_hooks", file=sys.stderr)
        return 2

    cat_bin = sys.argv[1]
    hook_bin = sys.argv[2]

    expect(Path(cat_bin).is_file(), f"missing binary: {cat_bin}")
    expect(Path(hook_bin).is_file(), f"missing hook binary: {hook_bin}")

    with tempfile.TemporaryDirectory(prefix="cat-reg-") as tmpdir:
        td = Path(tmpdir)
        src = td / "src.bin"

        rng = random.Random(1001)
        payload = bytes(rng.getrandbits(8) for _ in range(512 * 1024))
        src.write_bytes(payload)

        base_env = os.environ.copy()

        # Deterministic short-write loop validation.
        env = base_env.copy()
        env["CAT_TEST_WRITE_MAX"] = "7"
        rc, out, err = run_cmd(hook_bin, [str(src)], env=env)
        expect_eq(rc, 0, "short-write hook exit")
        expect_eq(out, payload, "short-write hook output")
        expect_eq(err, b"", "short-write hook stderr")

        # EINTR on write path.
        env = base_env.copy()
        env["CAT_TEST_WRITE_EINTR_EVERY"] = "4"
        rc, out, err = run_cmd(hook_bin, [str(src)], env=env)
        expect_eq(rc, 0, "write EINTR hook exit")
        expect_eq(out, payload, "write EINTR hook output")
        expect_eq(err, b"", "write EINTR hook stderr")

        # Deterministic short-read loop validation.
        env = base_env.copy()
        env["CAT_TEST_READ_MAX"] = "3"
        rc, out, err = run_cmd(hook_bin, [str(src)], env=env)
        expect_eq(rc, 0, "short-read hook exit")
        expect_eq(out, payload, "short-read hook output")
        expect_eq(err, b"", "short-read hook stderr")

        # EINTR on read path.
        env = base_env.copy()
        env["CAT_TEST_READ_EINTR_EVERY"] = "5"
        rc, out, err = run_cmd(hook_bin, [str(src)], env=env)
        expect_eq(rc, 0, "read EINTR hook exit")
        expect_eq(out, payload, "read EINTR hook output")
        expect_eq(err, b"", "read EINTR hook stderr")

        # Malloc failure fallback path for large -B.
        env = base_env.copy()
        env["CAT_TEST_MALLOC_FAIL"] = "1"
        rc, out, err = run_cmd(hook_bin, ["-B", "262144", str(src)], env=env)
        expect_eq(rc, 0, "malloc fallback exit")
        expect_eq(out, payload, "malloc fallback output")
        expect(b"fallback" in err, "malloc fallback warning missing")

        # Lock retry path on EINTR (injected).
        env = base_env.copy()
        env["CAT_TEST_LOCK_EINTR_COUNT"] = "4"
        rc, out, err = run_cmd(hook_bin, ["-l", str(src)], env=env)
        expect_eq(rc, 0, "lock EINTR retry exit")
        expect_eq(out, payload, "lock EINTR retry output")
        expect_eq(err, b"", "lock EINTR retry stderr")

        # Lock failure policy path.
        lock_errno = getattr(errno, "EOPNOTSUPP", errno.ENOSYS)
        env = base_env.copy()
        env["CAT_TEST_LOCK_FAIL_ERRNO"] = str(lock_errno)
        rc, _, err = run_cmd(hook_bin, ["-l", str(src)], env=env)
        expect(rc != 0, "lock failure should be nonzero")
        expect(b"unable to lock stdout" in err, "lock failure message missing")

        # EPIPE is handled gracefully (no stderr, successful early exit).
        env = base_env.copy()
        env["CAT_TEST_WRITE_EPIPE_ONCE"] = "1"
        rc, _, err = run_cmd(hook_bin, [str(src)], env=env)
        expect_eq(rc, 0, "EPIPE graceful exit")
        expect_eq(err, b"", "EPIPE stderr should be empty")

        # Real fcntl lock waiting behavior.
        out_file = td / "locked-out.bin"
        out_file.write_bytes(b"")
        holder_code = r"""
import fcntl
import os
import sys
import time

path = sys.argv[1]
hold = float(sys.argv[2])
fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o644)
fcntl.lockf(fd, fcntl.LOCK_EX)
sys.stdout.buffer.write(b"1")
sys.stdout.flush()
time.sleep(hold)
fcntl.lockf(fd, fcntl.LOCK_UN)
os.close(fd)
"""
        holder = subprocess.Popen(
            [sys.executable, "-c", holder_code, str(out_file), "1.5"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        ready = holder.stdout.read(1) if holder.stdout is not None else b""
        expect_eq(ready, b"1", "lock holder readiness")

        start = time.monotonic()
        with out_file.open("wb") as fp:
            proc = subprocess.run(
                [cat_bin, "-l", str(src)],
                stdout=fp,
                stderr=subprocess.PIPE,
                check=False,
                timeout=10,
            )
        elapsed = time.monotonic() - start

        holder.wait(timeout=5)
        holder_stderr = holder.stderr.read() if holder.stderr is not None else b""
        expect_eq(holder.returncode, 0, f"lock holder exit stderr={holder_stderr!r}")
        expect_eq(proc.returncode, 0, "real lock run exit")
        expect(elapsed >= 1.2, f"lock wait too short: {elapsed}")
        expect_eq(out_file.read_bytes(), payload, "real lock output")

    print("regression tests passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1)
