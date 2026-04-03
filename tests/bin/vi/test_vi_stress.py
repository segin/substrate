#!/usr/bin/env python3

import importlib.util
import os
import sys


def require(cond, msg):
    if not cond:
        print(f"FAIL: {msg}", file=sys.stderr)
        raise SystemExit(1)


def load_pty_helpers():
    helper_path = os.path.join(os.path.dirname(__file__), "test_vi_pty.py")
    spec = importlib.util.spec_from_file_location("test_vi_pty", helper_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/vi", file=sys.stderr)
        return 2

    vi_path = sys.argv[1]
    helpers = load_pty_helpers()

    large_lines = "".join(f"line {i:04d}\n" for i in range(1, 1501))
    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        large_lines,
        [b"1500G", b"0", b"r", b"Z", b"g", b"g", b"/line 0750\r", b"0", b"r", b"Y"],
        session_timeout=30.0,
    )
    require(exit_code == 0, f"large-file stress exited with status {exit_code}")
    require("line 1500/1500" in decoded, "missing large-file tail navigation status")
    require("line 750/1500" in decoded, "missing large-file search navigation status")
    saved_lines = saved.splitlines()
    require(len(saved_lines) == 1500, f"large-file line count mismatch: {len(saved_lines)}")
    require(saved_lines[749] == "Yine 0750",
            f"large-file search edit mismatch: {saved_lines[749]!r}")
    require(saved_lines[1499] == "Zine 1500",
            f"large-file tail edit mismatch: {saved_lines[1499]!r}")

    long_line = "a" * 400 + "\nsecond\n"
    steps = [
        ("winsize", 24, 20, 0.2),
        b"2", b"0", b"0", b"l",
        ("winsize", 24, 12, 0.2),
        b"1", b"0", b"0", b"l",
        ("winsize", 18, 40, 0.2),
        ("winsize", 30, 16, 0.2),
        ("winsize", 22, 60, 0.2),
        b"r", b"Z",
    ]
    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        long_line,
        steps,
        rows=12,
        cols=18,
        session_timeout=25.0,
    )
    require(exit_code == 0, f"long-line stress exited with status {exit_code}")
    first, second = saved.splitlines()
    require(len(first) == 400, f"long-line length mismatch: {len(first)}")
    require(first[300] == "Z", "long-line horizontal-scroll edit landed at wrong column")
    require(second == "second", f"long-line second line changed unexpectedly: {second!r}")
    require("line 1/2" in decoded, "missing long-line status output")

    resize_lines = "".join(f"row {i:03d}\n" for i in range(1, 401))
    resize_steps = []
    for dims in [(8, 20), (30, 100), (12, 30), (18, 50), (40, 120),
                 (10, 18), (24, 80), (16, 40), (32, 90), (14, 24)]:
        resize_steps.append(("winsize", dims[0], dims[1], 0.15))
    resize_steps.extend([b"2", b"0", b"0", b"G", b"0", b"r", b"Q"])
    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        resize_lines,
        resize_steps,
        rows=14,
        cols=24,
        session_timeout=30.0,
    )
    require(exit_code == 0, f"resize stress exited with status {exit_code}")
    require("line 200/400" in decoded, "missing repeated-resize target status")
    require(saved.splitlines()[199] == "Qow 200",
            f"repeated-resize edit mismatch: {saved.splitlines()[199]!r}")

    undo_steps = []
    expected = "base"
    for i in range(32):
        ch = bytes([ord('0') + (i % 10)])

        undo_steps.extend([b"A", ch, b"\x1b", b"u", b"\x12"])
        expected += chr(ord('0') + (i % 10))
    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "base\n",
        undo_steps,
        session_timeout=35.0,
    )
    require(exit_code == 0, f"undo/redo stress exited with status {exit_code}")
    require(saved == expected + "\n",
            f"undo/redo stress buffer mismatch: {saved!r}")
    require(decoded.count("line 1/1") >= 1, "missing undo/redo status output")

    print("vi stress tests: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
