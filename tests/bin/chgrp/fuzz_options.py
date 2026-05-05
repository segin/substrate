#!/usr/bin/env python3
import os
import random
import shutil
import subprocess
import sys
import tempfile


SEED = int(os.environ.get("CHGRP_FUZZ_SEED", "1717"))
ITERATIONS = int(os.environ.get("CHGRP_FUZZ_ITERS", "120"))


def random_token(rng: random.Random) -> str:
    choices = [
        "-h",
        "-R",
        "-RH",
        "-RP",
        "-hR",
        "--help",
        "--version",
        "0",
        "1:2",
        "5",
        "-r",
        "--reference=",
        "nonexistent",
        "a/b/c",
        "--bogus",
    ]
    if rng.random() < 0.3:
        alphabet = ":/_=," + "abcdefghijklmnopqrstuvwxyz0123456789"
        return "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 12)))
    return rng.choice(choices)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fuzz_options.py /path/to/chgrp_host", file=sys.stderr)
        return 1

    rng = random.Random(SEED)
    binary = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp(prefix="chgrp-fuzz-opt-")
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
                raise AssertionError(f"chgrp_host crashed with args={args!r}")
    finally:
        shutil.rmtree(tmp)

    print(f"fuzz_options: ok (seed={SEED}, iterations={ITERATIONS})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
