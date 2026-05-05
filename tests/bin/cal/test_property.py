#!/usr/bin/env python3
import random
import re
import subprocess
import sys


SEED = 2323
ITERATIONS = 80


def gregorian_leap(year: int) -> bool:
    return year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)


def julian_leap(year: int) -> bool:
    return year % 4 == 0


def days_in_month(year: int, month: int, leap_fn) -> int:
    base = [0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    if month == 2 and leap_fn(year):
        return 29
    return base[month]


def tokens(output: str) -> set[int]:
    cleaned = re.sub(r"\x1b\[[0-9;]*m", "", output)
    return {int(token) for token in re.findall(r"\b\d{1,3}\b", cleaned) if 1 <= int(token) <= 366}


def run(binary: str, *args: str) -> str:
    completed = subprocess.run([binary, *args], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, timeout=5)
    if completed.returncode != 0:
        raise AssertionError(completed.stderr)
    return completed.stdout


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_property.py /path/to/cal", file=sys.stderr)
        return 1

    binary = sys.argv[1]
    rng = random.Random(SEED)

    for _ in range(ITERATIONS):
        year = rng.randint(1, 9999)
        month = rng.randint(1, 12)

        output = run(binary, "--gregorian", str(month), str(year))
        day_tokens = {value for value in tokens(output) if value <= 31}
        expected_count = days_in_month(year, month, gregorian_leap)
        if len(day_tokens) != expected_count:
            raise AssertionError(f"Gregorian month count mismatch for {month}/{year}")

        output = run(binary, "--julian", str(month), str(year))
        day_tokens = {value for value in tokens(output) if value <= 31}
        expected_count = days_in_month(year, month, julian_leap)
        if len(day_tokens) != expected_count:
            raise AssertionError(f"Julian month count mismatch for {month}/{year}")

    print(f"test_property: ok (seed={SEED}, iterations={ITERATIONS})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())