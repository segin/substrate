#!/usr/bin/env python3
import os
import random
import shutil
import subprocess
import sys
import tempfile


SEED = int(os.environ.get("RMDIR_PATH_FUZZ_SEED", "9292"))
ITERATIONS = int(os.environ.get("RMDIR_PATH_FUZZ_ITERS", "80"))


def random_name(rng: random.Random) -> bytes:
    alphabet = bytes(ch for ch in range(1, 256) if ch not in (47,))
    length = rng.randint(1, 24)
    return bytes(rng.choice(alphabet) for _ in range(length))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fuzz_paths.py /path/to/rmdir_host", file=sys.stderr)
        return 1

    rng = random.Random(SEED)
    binary = os.fsencode(os.path.abspath(sys.argv[1]))
    workspace = tempfile.mkdtemp(prefix="rmdir-path-fuzz-")
    workspace_b = os.fsencode(workspace)
    try:
        for _ in range(ITERATIONS):
            name = random_name(rng)
            target = os.path.join(workspace_b, name)
            try:
                os.mkdir(target)
            except OSError:
                continue
            completed = subprocess.run([binary, b"-v", name],
                                       cwd=workspace_b,
                                       stdin=subprocess.DEVNULL,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       timeout=3)
            if completed.returncode != 0:
                raise AssertionError(f"rmdir_host failed for path {name!r}")
        print(f"fuzz_paths: ok (seed={SEED}, iterations={ITERATIONS})")
        return 0
    finally:
        shutil.rmtree(workspace, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())