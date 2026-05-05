#!/usr/bin/env python3
import os
import random
import shutil
import subprocess
import sys
import tempfile


SEED = int(os.environ.get("RMDIR_FUZZ_SEED", "8181"))
ITERATIONS = int(os.environ.get("RMDIR_FUZZ_ITERS", "120"))


def random_token(rng: random.Random) -> str:
    choices = [
        "-p",
        "-v",
        "--parents",
        "--verbose",
        "--ignore-fail-on-non-empty",
        "--help",
        "--version",
        "name",
        "nested/path",
        "",
        "--bogus",
    ]
    if rng.random() < 0.2:
        alphabet = "-_=:/" + "abcdefghijklmnopqrstuvwxyz0123456789"
        return "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 12)))
    return rng.choice(choices)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fuzz_options.py /path/to/rmdir_host", file=sys.stderr)
        return 1

    rng = random.Random(SEED)
    binary = os.path.abspath(sys.argv[1])
    temporary = tempfile.mkdtemp(prefix="rmdir-fuzz-")
    try:
        for _ in range(ITERATIONS):
            argc = rng.randint(0, 6)
            args = [random_token(rng) for _ in range(argc)]
            completed = subprocess.run([binary, *args], cwd=temporary,
                                       stdin=subprocess.DEVNULL,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       text=False,
                                       timeout=3)
            if completed.returncode < 0:
                raise AssertionError(f"rmdir_host crashed with args={args!r}")
        print(f"fuzz_options: ok (seed={SEED}, iterations={ITERATIONS})")
        return 0
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())