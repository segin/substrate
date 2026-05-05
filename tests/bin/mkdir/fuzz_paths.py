#!/usr/bin/env python3
import os
import random
import shutil
import subprocess
import sys
import tempfile


SEED = int(os.environ.get("MKDIR_FUZZ_PATH_SEED", "1818"))
ITERATIONS = int(os.environ.get("MKDIR_FUZZ_PATH_ITERS", "120"))


def random_segment_bytes(rng: random.Random) -> bytes:
    size = rng.randint(1, 8)
    out = bytearray()
    for _ in range(size):
        value = rng.randint(1, 255)
        while value in (0, ord('/')):
            value = rng.randint(1, 255)
        out.append(value)
    return bytes(out)


def random_path_bytes(rng: random.Random) -> bytes:
    parts = [random_segment_bytes(rng) for _ in range(rng.randint(1, 4))]
    return b"/".join(parts)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fuzz_paths.py /path/to/mkdir_host", file=sys.stderr)
        return 1

    rng = random.Random(SEED)
    tmp = tempfile.mkdtemp(prefix="mkdir-fuzz-path-")
    try:
        binary = os.fsencode(os.path.abspath(sys.argv[1]))
        for _ in range(ITERATIONS):
            path = random_path_bytes(rng)
            cp = subprocess.run([binary, b"-p", path], cwd=tmp,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                timeout=3)
            if cp.returncode < 0:
                raise AssertionError("mkdir_host crashed while fuzzing path handling")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"fuzz_paths: ok (seed={SEED}, iterations={ITERATIONS})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())