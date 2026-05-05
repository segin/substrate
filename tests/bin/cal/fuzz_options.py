#!/usr/bin/env python3
import random
import subprocess
import sys


SEED = 2424
ITERATIONS = 150


def random_token(rng: random.Random) -> str:
    choices = [
        "-1",
        "-3",
        "-y",
        "-m",
        "-s",
        "-j",
        "-w",
        "-h",
        "-n", "2",
        "-A", "1",
        "-B", "1",
        "--gregorian",
        "--julian",
        "--color=auto",
        "--color=always",
        "--color=never",
        "--reform=1752-09-14",
        "2024",
        "9",
        "--help",
        "--version",
        "--bogus",
    ]
    if rng.random() < 0.2:
        alphabet = "-_=:/" + "abcdefghijklmnopqrstuvwxyz0123456789"
        return "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 12)))
    return rng.choice(choices)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fuzz_options.py /path/to/cal", file=sys.stderr)
        return 1

    binary = sys.argv[1]
    rng = random.Random(SEED)
    for _ in range(ITERATIONS):
        argc = rng.randint(0, 7)
        args = [random_token(rng) for _ in range(argc)]
        completed = subprocess.run([binary, *args], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, stdin=subprocess.DEVNULL,
                                   timeout=3)
        if completed.returncode < 0:
            raise AssertionError(f"cal crashed with args={args!r}")
    print(f"fuzz_options: ok (seed={SEED}, iterations={ITERATIONS})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())