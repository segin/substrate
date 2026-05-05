#!/usr/bin/env python3
import re
import subprocess
import sys


def run(binary: str, *args: str) -> str:
    completed = subprocess.run([binary, *args], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, timeout=5)
    if completed.returncode != 0:
        raise AssertionError(completed.stderr)
    return completed.stdout


def extract_day_tokens(output: str) -> set[int]:
    tokens: set[int] = set()
    for line in output.splitlines()[2:]:
        line = re.sub(r"\x1b\[[0-9;]*m", "", line)
        for token in re.findall(r"\b\d{1,3}\b", line):
            value = int(token)
            if 1 <= value <= 366:
                tokens.add(value)
    return tokens


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_reference.py /path/to/cal", file=sys.stderr)
        return 1

    binary = sys.argv[1]

    september = run(binary, "9", "1752")
    days = extract_day_tokens(september)
    expected = {1, 2, *range(14, 31)}
    if days != expected:
        raise AssertionError(f"September 1752 mismatch: {sorted(days)}")

    feb_greg = run(binary, "--gregorian", "2", "1900")
    if 29 in extract_day_tokens(feb_greg):
        raise AssertionError("Gregorian February 1900 incorrectly contains day 29")

    feb_jul = run(binary, "--julian", "2", "1900")
    if 29 not in extract_day_tokens(feb_jul):
        raise AssertionError("Julian February 1900 missing day 29")

    julian_days = run(binary, "-j", "2", "2024")
    if 60 not in extract_day_tokens(julian_days):
        raise AssertionError("Julian day output missing ordinal 60 for 2024-02-29")

    print("test_reference: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())