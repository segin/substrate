#!/usr/bin/env python3
import os
import random
import shutil
import subprocess
import sys
import tempfile


SEED = int(os.environ.get("MKDIR_FUZZ_OPT_SEED", "1717"))
ITERATIONS = int(os.environ.get("MKDIR_FUZZ_OPT_ITERS", "120"))


def random_token(rng: random.Random) -> str:
    choices = [
        "-p",
        "-v",
        "-pv",
        "-m",
        "700",
        "u=rwx,go=",
        "--help",
        "--version",
        "--parents",
        "--verbose",
        "--mode=755",
        "--context=test:test:test",
        "name",
        "nested/path",
        "",
        "--bogus",
        "-Z",
        "-Zctx",
    ]
    if rng.random() < 0.2:
        alphabet = "-_=,:/" + "abcdefghijklmnopqrstuvwxyz0123456789"
        return "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 12)))
    return rng.choice(choices)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fuzz_options.py /path/to/mkdir_host", file=sys.stderr)
        return 1

    rng = random.Random(SEED)
    binary = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp(prefix="mkdir-fuzz-opt-")
    try:
        for _ in range(ITERATIONS):
            argc = rng.randint(0, 6)
            args = [random_token(rng) for _ in range(argc)]
            cp = subprocess.run([binary, *args], cwd=tmp,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                text=False,
                                timeout=3)
            if cp.returncode < 0:
                raise AssertionError(f"mkdir_host crashed with args={args!r}")
    finally:
        shutil.rmtree(tmp)

    print(f"fuzz_options: ok (seed={SEED}, iterations={ITERATIONS})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())