#!/usr/bin/env python3

import argparse
import importlib.util
import os
import shutil
import sys
import tempfile


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


def run_vim_session(helpers, vim_path, initial_text, key_steps, rows=24, cols=80,
                    final_keys=b":wq\r", extra_files=None, file_args=None):
    env_path = shutil.which("env") or "/usr/bin/env"
    extra_args = [
        f"HOME={tempfile.gettempdir()}",
        "VIMINIT=set nomore|set viminfo=",
        vim_path,
        "-Nu", "NONE",
        "-i", "NONE",
        "-n",
    ]
    return helpers.run_vi_session(
        env_path,
        initial_text,
        key_steps,
        rows=rows,
        cols=cols,
        final_keys=final_keys,
        extra_files=extra_files,
        extra_args=extra_args,
        argv0="env",
        file_args=file_args,
    )


def compare_case(helpers, vi_path, vim_path, name, initial_text, key_steps, rows=24,
                 cols=80, final_keys=b":wq\r", extra_files=None, file_args=None):
    vi_exit, _, vi_saved = helpers.run_vi_session(
        vi_path,
        initial_text,
        key_steps,
        rows=rows,
        cols=cols,
        final_keys=final_keys,
        extra_files=extra_files,
        file_args=file_args,
    )
    vim_exit, _, vim_saved = run_vim_session(
        helpers,
        vim_path,
        initial_text,
        key_steps,
        rows=rows,
        cols=cols,
        final_keys=final_keys,
        extra_files=extra_files,
        file_args=file_args,
    )
    require(vi_exit == 0, f"{name}: vi exited with status {vi_exit}")
    require(vim_exit == 0, f"{name}: vim exited with status {vim_exit}")
    require(vi_saved == vim_saved,
            f"{name}: vi {vi_saved!r} != vim {vim_saved!r}")
    print(f"PASS: {name}", flush=True)


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Compare Substrate vi against sanitized vim overlap cases.",
    )
    parser.add_argument("vi_path", help="path to the Substrate vi binary")
    parser.add_argument(
        "--list",
        action="store_true",
        help="list selected oracle case names and exit",
    )
    parser.add_argument(
        "--match",
        action="append",
        default=[],
        metavar="SUBSTR",
        help="run only cases whose names contain SUBSTR; repeatable",
    )
    parser.add_argument(
        "--start-at",
        metavar="CASE",
        help="start at CASE (inclusive) in oracle order",
    )
    parser.add_argument(
        "--limit",
        type=int,
        metavar="N",
        help="run at most N selected cases",
    )
    args = parser.parse_args(argv[1:])
    if args.limit is not None and args.limit < 0:
        parser.error("--limit must be non-negative")
    return args


def main():
    args = parse_args(sys.argv)
    vi_path = args.vi_path
    cases = [
        {
            "name": "search-column-repeat",
            "initial_text": "one two three\nalpha two beta\n",
            "key_steps": [b"/", b"two\r", b"r", b"Z", b"n", b"r", b"Y"],
        },
        {
            "name": "utf8-motion-0ll",
            "initial_text": "aéb\n",
            "key_steps": [b"0", b"l", b"l", b"r", b"X"],
        },
        {
            "name": "utf8-motion-dollar-hh",
            "initial_text": "aéb\n",
            "key_steps": [b"$", b"h", b"h", b"r", b"X"],
        },
        {
            "name": "utf8-motion-caret-ll",
            "initial_text": "  aéb\n",
            "key_steps": [b"^", b"l", b"l", b"r", b"X"],
        },
        {
            "name": "utf8-motion-bar",
            "initial_text": "aéb\n",
            "key_steps": [b"3|", b"r", b"X"],
        },
        {
            "name": "utf8-motion-till-forward",
            "initial_text": "aébc\n",
            "key_steps": [b"0", b"t", b"b", b"i", b"X", b"\x1b"],
        },
        {
            "name": "utf8-motion-till-backward",
            "initial_text": "abécd\n",
            "key_steps": [b"$", b"T", b"b", b"i", b"X", b"\x1b"],
        },
        {
            "name": "utf8-motion-find-forward",
            "initial_text": "aébcd\n",
            "key_steps": [b"0", b"f", b"c", b"r", b"X"],
        },
        {
            "name": "utf8-motion-find-backward",
            "initial_text": "abécd\n",
            "key_steps": [b"$", b"F", b"b", b"r", b"X"],
        },
        {
            "name": "utf8-motion-repeat-find",
            "initial_text": "aébcdebc\n",
            "key_steps": [b"0", b"f", b"b", b";", b"r", b"X"],
        },
        {
            "name": "utf8-motion-repeat-reverse",
            "initial_text": "abécdbfg\n",
            "key_steps": [b"$", b"F", b"b", b",", b"r", b"X"],
        },
        {
            "name": "utf8-motion-percent",
            "initial_text": "(é)\n",
            "key_steps": [b"%", b"r", b"X"],
        },
        {
            "name": "utf8-word-motion-w",
            "initial_text": "aé bc\n",
            "key_steps": [b"0", b"w", b"r", b"X"],
        },
        {
            "name": "utf8-word-motion-b",
            "initial_text": "a bcé\n",
            "key_steps": [b"$", b"b", b"r", b"X"],
        },
        {
            "name": "utf8-word-motion-e",
            "initial_text": "aébc de\n",
            "key_steps": [b"0", b"e", b"r", b"X"],
        },
        {
            "name": "utf8-word-motion-ge",
            "initial_text": "ab cé\n",
            "key_steps": [b"$", b"g", b"e", b"i", b"X", b"\x1b"],
        },
        {
            "name": "utf8-bigword-motion-W",
            "initial_text": "aé bc\n",
            "key_steps": [b"0", b"W", b"r", b"X"],
        },
        {
            "name": "utf8-bigword-motion-B",
            "initial_text": "a bcé\n",
            "key_steps": [b"$", b"B", b"r", b"X"],
        },
        {
            "name": "utf8-bigword-motion-E",
            "initial_text": "aébc de\n",
            "key_steps": [b"0", b"E", b"r", b"X"],
        },
        {
            "name": "utf8-bigword-motion-gE",
            "initial_text": "ab cé\n",
            "key_steps": [b"$", b"g", b"E", b"i", b"X", b"\x1b"],
        },
        {
            "name": "utf8-sentence-motion",
            "initial_text": "Hi é. Bye.\n",
            "key_steps": [b"0", b")", b"r", b"X"],
        },
        {
            "name": "utf8-paragraph-motion",
            "initial_text": "aa\n\néé\n\nbb\n",
            "key_steps": [b"0", b"}", b"r", b"X"],
        },
        {
            "name": "unshift-search-forward",
            "initial_text": "\taa\n\tmatch\n\tbb\n\tmatch\n\tcc\n",
            "key_steps": [b"<", b"/", b"match\r"],
        },
        {
            "name": "shift-search-backward",
            "initial_text": "\taa\n\tmatch\n\tbb\n\tmatch\n\tcc\n",
            "key_steps": [b"G", b">", b"?", b"match\r"],
        },
        {
            "name": "unshift-search-backward",
            "initial_text": "\taa\n\tmatch\n\tbb\n\tmatch\n\tcc\n",
            "key_steps": [b"G", b"<", b"?", b"match\r"],
        },
        {
            "name": "change-repeat-search-forward",
            "initial_text": "aa\nmatch\nbb\nmatch\ncc\n",
            "key_steps": [b"/", b"match\r", b"g", b"g", b"c", b"n", b"X", b"\x1b"],
        },
        {
            "name": "delete-repeat-search-backward",
            "initial_text": "aa\nfoo\nbb\nfoo\ncc\n",
            "key_steps": [b"G", b"?", b"foo\r", b"d", b"N"],
        },
        {
            "name": "change-repeat-search-backward",
            "initial_text": "aa\nfoo\nbb\nfoo\ncc\n",
            "key_steps": [b"G", b"?", b"foo\r", b"c", b"N", b"X", b"\x1b"],
        },
        {
            "name": "shift-repeat-search-backward",
            "initial_text": "\taa\n\tfoo\n\tbb\n\tfoo\n\tcc\n",
            "key_steps": [b"G", b"?", b"foo\r", b"g", b"g", b">", b"N"],
        },
        {
            "name": "unshift-repeat-search-forward",
            "initial_text": "\taa\n\tmatch\n\tbb\n\tmatch\n\tcc\n",
            "key_steps": [b"/", b"match\r", b"g", b"g", b"<", b"n"],
        },
        {
            "name": "unshift-repeat-search-backward",
            "initial_text": "\taa\n\tfoo\n\tbb\n\tfoo\n\tcc\n",
            "key_steps": [b"G", b"?", b"foo\r", b"g", b"g", b"<", b"N"],
        },
        {
            "name": "shift-star-search",
            "initial_text": "\tword\n\tmiddle\n\tword\n",
            "key_steps": [b">", b"*"],
        },
        {
            "name": "unshift-star-search",
            "initial_text": "\tword\n\tmiddle\n\tword\n",
            "key_steps": [b"<", b"*"],
        },
        {
            "name": "shift-hash-search",
            "initial_text": "\tword\n\tmiddle\n\tword\n",
            "key_steps": [b"G", b">", b"#"],
        },
        {
            "name": "unshift-hash-search",
            "initial_text": "\tword\n\tmiddle\n\tword\n",
            "key_steps": [b"G", b"<", b"#"],
        },
        {
            "name": "change-find-forward",
            "initial_text": "abczdef\n",
            "key_steps": [b"0", b"c", b"f", b"z", b"X", b"\x1b"],
        },
        {
            "name": "delete-till-forward",
            "initial_text": "abczdef\n",
            "key_steps": [b"0", b"d", b"t", b"z"],
        },
        {
            "name": "yank-till-forward",
            "initial_text": "abczdef\n",
            "key_steps": [b"0", b"y", b"t", b"z", b"P"],
        },
        {
            "name": "shift-find-forward",
            "initial_text": "abczdef\n",
            "key_steps": [b">", b"f", b"z"],
        },
        {
            "name": "unshift-find-forward",
            "initial_text": "\tabczdef\n",
            "key_steps": [b"<", b"f", b"z"],
        },
        {
            "name": "shift-till-forward",
            "initial_text": "abczdef\n",
            "key_steps": [b">", b"t", b"z"],
        },
        {
            "name": "unshift-till-forward",
            "initial_text": "\tabczdef\n",
            "key_steps": [b"<", b"t", b"z"],
        },
        {
            "name": "change-find-backward",
            "initial_text": "abczdef\n",
            "key_steps": [b"$", b"c", b"F", b"b", b"X", b"\x1b"],
        },
        {
            "name": "yank-find-backward",
            "initial_text": "abczdef\n",
            "key_steps": [b"$", b"y", b"F", b"b", b"P"],
        },
        {
            "name": "delete-till-backward",
            "initial_text": "abczdef\n",
            "key_steps": [b"$", b"d", b"T", b"b"],
        },
        {
            "name": "yank-till-backward",
            "initial_text": "abczdef\n",
            "key_steps": [b"$", b"y", b"T", b"b", b"P"],
        },
        {
            "name": "shift-find-backward",
            "initial_text": "abczdef\n",
            "key_steps": [b"$", b">", b"F", b"b"],
        },
        {
            "name": "unshift-find-backward",
            "initial_text": "\tabczdef\n",
            "key_steps": [b"$", b"<", b"F", b"b"],
        },
        {
            "name": "shift-till-backward",
            "initial_text": "abczdef\n",
            "key_steps": [b"$", b">", b"T", b"b"],
        },
        {
            "name": "unshift-till-backward",
            "initial_text": "\tabczdef\n",
            "key_steps": [b"$", b"<", b"T", b"b"],
        },
        {
            "name": "delete-repeat-find-forward",
            "initial_text": "alpha beta gamma\n",
            "key_steps": [b"0", b"f", b"a", b"d", b";"],
        },
        {
            "name": "change-repeat-find-forward",
            "initial_text": "alpha beta gamma\n",
            "key_steps": [b"0", b"f", b"a", b"c", b";", b"X", b"\x1b"],
        },
        {
            "name": "yank-repeat-find-forward",
            "initial_text": "alpha beta gamma\n",
            "key_steps": [b"0", b"f", b"a", b"y", b";", b"P"],
        },
        {
            "name": "shift-repeat-find-forward",
            "initial_text": "alpha beta gamma\n",
            "key_steps": [b"0", b"f", b"a", b">", b";"],
        },
        {
            "name": "unshift-repeat-find-forward",
            "initial_text": "\talpha beta gamma\n",
            "key_steps": [b"0", b"f", b"a", b"<", b";"],
        },
        {
            "name": "delete-repeat-find-backward",
            "initial_text": "alpha beta gamma\n",
            "key_steps": [b"$", b"F", b"a", b"d", b","],
        },
        {
            "name": "change-repeat-find-backward",
            "initial_text": "alpha beta gamma\n",
            "key_steps": [b"$", b"F", b"a", b"c", b",", b"X", b"\x1b"],
        },
        {
            "name": "yank-repeat-find-backward",
            "initial_text": "alpha beta gamma\n",
            "key_steps": [b"$", b"F", b"a", b"y", b",", b"P"],
        },
        {
            "name": "shift-repeat-find-backward",
            "initial_text": "alpha beta gamma\n",
            "key_steps": [b"$", b"F", b"a", b">", b","],
        },
        {
            "name": "unshift-repeat-find-backward",
            "initial_text": "\talpha beta gamma\n",
            "key_steps": [b"$", b"F", b"a", b"<", b","],
        },
        {
            "name": "delete-line-mark",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"j", b"m", b"a", b"G", b"d", b"'", b"a"],
        },
        {
            "name": "change-line-mark",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"j", b"m", b"a", b"G", b"c", b"'", b"a", b"X", b"\x1b"],
        },
        {
            "name": "yank-line-mark",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"j", b"m", b"a", b"G", b"y", b"'", b"a", b"P"],
        },
        {
            "name": "shift-line-mark",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"j", b"m", b"a", b"G", b">", b"'", b"a"],
        },
        {
            "name": "unshift-line-mark",
            "initial_text": "\tone\n\ttwo\n\tthree\n\tfour\n",
            "key_steps": [b"j", b"m", b"a", b"G", b"<", b"'", b"a"],
        },
        {
            "name": "counted-line-down-motion",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"2", b"j", b"r", b"X"],
        },
        {
            "name": "counted-line-up-motion",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"G", b"2", b"k", b"r", b"X"],
        },
        {
            "name": "counted-plus-motion",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"3", b"+", b"r", b"X"],
        },
        {
            "name": "counted-minus-motion",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"G", b"2", b"-", b"r", b"X"],
        },
        {
            "name": "counted-underscore-motion",
            "initial_text": "  one\n  two\n  three\n",
            "key_steps": [b"2", b"_", b"r", b"X"],
        },
        {
            "name": "counted-screen-top-motion",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\n",
            "key_steps": [b"4", b"G", b"2", b"H", b"r", b"X"],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "screen-middle-motion",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\n",
            "key_steps": [b"5", b"G", b"M", b"r", b"X"],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "counted-screen-bottom-motion",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\n",
            "key_steps": [b"1", b"G", b"2", b"L", b"r", b"X"],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "counted-half-page-down-motion",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nline 10\nline 11\nline 12\nline 13\nline 14\nline 15\nline 16\nline 17\nline 18\nline 19\n",
            "key_steps": [b"5", b"G", b"2", b"\x04", b"r", b"X"],
            "rows": 8,
            "cols": 20,
        },
        {
            "name": "counted-half-page-up-motion",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nline 10\nline 11\nline 12\nline 13\nline 14\nline 15\nline 16\nline 17\nline 18\nline 19\n",
            "key_steps": [b"1", b"5", b"G", b"2", b"\x15", b"r", b"X"],
            "rows": 8,
            "cols": 20,
        },
        {
            "name": "counted-line-scroll-down-motion",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nline 10\nline 11\nline 12\nline 13\nline 14\nline 15\nline 16\nline 17\nline 18\nline 19\n",
            "key_steps": [b"5", b"G", b"z", b"\r", b"2", b"\x05", b"r", b"X"],
            "rows": 8,
            "cols": 20,
        },
        {
            "name": "line-scroll-up-motion",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\n",
            "key_steps": [b"5", b"G", b"z", b"\r", b"\x19", b"r", b"Z"],
            "rows": 5,
            "cols": 20,
        },
        {
            "name": "counted-line-scroll-up-motion",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\n",
            "key_steps": [b"5", b"G", b"z", b"\r", b"2", b"\x19", b"r", b"Z"],
            "rows": 5,
            "cols": 20,
        },
        {
            "name": "counted-word-forward-motion",
            "initial_text": "one two three\nfour five six\n",
            "key_steps": [b"2", b"w", b"r", b"X"],
        },
        {
            "name": "counted-bigword-forward-motion",
            "initial_text": "one,two three\nfour,five six\n",
            "key_steps": [b"2", b"W", b"r", b"X"],
        },
        {
            "name": "counted-word-end-motion",
            "initial_text": "one two three\nfour five six\n",
            "key_steps": [b"2", b"e", b"r", b"X"],
        },
        {
            "name": "counted-bigword-end-motion",
            "initial_text": "one,two three\nfour,five six\n",
            "key_steps": [b"2", b"E", b"r", b"X"],
        },
        {
            "name": "counted-word-backward-motion",
            "initial_text": "one two three\nfour five six\n",
            "key_steps": [b"G", b"2", b"b", b"r", b"X"],
        },
        {
            "name": "counted-bigword-backward-motion",
            "initial_text": "one,two three\nfour,five six\n",
            "key_steps": [b"G", b"2", b"B", b"r", b"X"],
        },
        {
            "name": "counted-word-end-backward-motion",
            "initial_text": "one two three\nfour five six\n",
            "key_steps": [b"G", b"2", b"g", b"e", b"r", b"X"],
        },
        {
            "name": "counted-bigword-end-backward-motion",
            "initial_text": "one,two three\nfour,five six\n",
            "key_steps": [b"G", b"2", b"g", b"E", b"r", b"X"],
        },
        {
            "name": "counted-sentence-forward-motion",
            "initial_text": "one. two. three.\nalpha. beta. gamma.\n",
            "key_steps": [b"$", b"2", b")", b"r", b"X"],
        },
        {
            "name": "counted-sentence-backward-motion",
            "initial_text": "one. two. three.\nalpha. beta. gamma.\n",
            "key_steps": [b"G", b"$", b"2", b"(", b"r", b"X"],
        },
        {
            "name": "counted-paragraph-forward-motion",
            "initial_text": "one\n\ntwo\n\nthree\n\nfour\n",
            "key_steps": [b"2", b"}", b"r", b"X"],
        },
        {
            "name": "counted-paragraph-backward-motion",
            "initial_text": "one\n\ntwo\n\nthree\n\nfour\n",
            "key_steps": [b"G", b"2", b"{", b"r", b"X"],
        },
        {
            "name": "counted-section-forward-start-motion",
            "initial_text": "intro\n{\nbody1\n}\nmid\n{\nbody2\n}\noutro\n",
            "key_steps": [b"2", b"]", b"]", b"r", b"X"],
        },
        {
            "name": "counted-section-backward-start-motion",
            "initial_text": "intro\n{\nbody1\n}\nmid\n{\nbody2\n}\noutro\n",
            "key_steps": [b"G", b"2", b"[", b"[", b"r", b"X"],
        },
        {
            "name": "counted-section-forward-end-motion",
            "initial_text": "intro\n{\nbody1\n}\nmid\n{\nbody2\n}\noutro\n",
            "key_steps": [b"2", b"]", b"[", b"r", b"X"],
        },
        {
            "name": "counted-section-backward-end-motion",
            "initial_text": "intro\n{\nbody1\n}\nmid\n{\nbody2\n}\noutro\n",
            "key_steps": [b"G", b"2", b"[", b"]", b"r", b"X"],
        },
        {
            "name": "delete-section-start-forward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"d", b"]", b"]"],
        },
        {
            "name": "change-section-start-forward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"c", b"]", b"]", b"X", b"\x1b"],
        },
        {
            "name": "yank-section-start-forward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"y", b"]", b"]", b"P"],
        },
        {
            "name": "shift-section-start-forward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b">", b"]", b"]"],
        },
        {
            "name": "unshift-section-start-forward",
            "initial_text": "\tone\n\t{\n\ttwo\n\t}\n\tthree\n\t{\n\tfour\n\t}\n",
            "key_steps": [b"<", b"]", b"]"],
        },
        {
            "name": "delete-section-start-backward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"G", b"d", b"[", b"["],
        },
        {
            "name": "change-section-start-backward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"G", b"c", b"[", b"[", b"X", b"\x1b"],
        },
        {
            "name": "yank-section-start-backward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"G", b"y", b"[", b"[", b"P"],
        },
        {
            "name": "shift-section-start-backward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"G", b">", b"[", b"["],
        },
        {
            "name": "unshift-section-start-backward",
            "initial_text": "\tone\n\t{\n\ttwo\n\t}\n\tthree\n\t{\n\tfour\n\t}\n",
            "key_steps": [b"G", b"<", b"[", b"["],
        },
        {
            "name": "delete-section-end-forward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"d", b"]", b"["],
        },
        {
            "name": "change-section-end-forward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"c", b"]", b"[", b"X", b"\x1b"],
        },
        {
            "name": "yank-section-end-forward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"y", b"]", b"[", b"P"],
        },
        {
            "name": "shift-section-end-forward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b">", b"]", b"["],
        },
        {
            "name": "unshift-section-end-forward",
            "initial_text": "\tone\n\t{\n\ttwo\n\t}\n\tthree\n\t{\n\tfour\n\t}\n",
            "key_steps": [b"<", b"]", b"["],
        },
        {
            "name": "delete-section-end-backward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"G", b"d", b"[", b"]"],
        },
        {
            "name": "change-section-end-backward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"G", b"c", b"[", b"]", b"X", b"\x1b"],
        },
        {
            "name": "yank-section-end-backward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"G", b"y", b"[", b"]", b"P"],
        },
        {
            "name": "shift-section-end-backward",
            "initial_text": "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            "key_steps": [b"G", b">", b"[", b"]"],
        },
        {
            "name": "unshift-section-end-backward",
            "initial_text": "\tone\n\t{\n\ttwo\n\t}\n\tthree\n\t{\n\tfour\n\t}\n",
            "key_steps": [b"G", b"<", b"[", b"]"],
        },
        {
            "name": "counted-search-repeat-n-motion",
            "initial_text": "target one\ntarget two\ntarget three\ntarget four\n",
            "key_steps": [b"/", b"target\r", b"2", b"n", b"r", b"X"],
        },
        {
            "name": "counted-search-repeat-N-motion",
            "initial_text": "target one\ntarget two\ntarget three\ntarget four\n",
            "key_steps": [b"G", b"?", b"target\r", b"2", b"N", b"r", b"X"],
        },
        {
            "name": "counted-star-search-motion",
            "initial_text": "word one\nword two\nword three\nword four\n",
            "key_steps": [b"2", b"*", b"r", b"X"],
        },
        {
            "name": "counted-hash-search-motion",
            "initial_text": "word one\nword two\nword three\nword four\n",
            "key_steps": [b"G", b"2", b"#", b"r", b"X"],
        },
        {
            "name": "counted-percent-motion",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nline 10\n",
            "key_steps": [b"5", b"0", b"%", b"r", b"X"],
        },
        {
            "name": "blank-word-forward-motion",
            "initial_text": "one\n\n two\nthree\n\n\nend\n",
            "key_steps": [b"w", b"r", b"X"],
        },
        {
            "name": "blank-word-forward-count-motion",
            "initial_text": "one\n\n two\nthree\n\n\nend\n",
            "key_steps": [b"2", b"w", b"r", b"X"],
        },
        {
            "name": "blank-word-end-count-motion",
            "initial_text": "one\n\n two\nthree\n\n\nend\n",
            "key_steps": [b"2", b"e", b"r", b"X"],
        },
        {
            "name": "blank-bigword-end-count-motion",
            "initial_text": "one\n\n two\nthree\n\n\nend\n",
            "key_steps": [b"2", b"E", b"r", b"X"],
        },
        {
            "name": "blank-word-backward-motion",
            "initial_text": "one\n\n two\nthree\n\n\nend\n",
            "key_steps": [b"G", b"b", b"r", b"X"],
        },
        {
            "name": "blank-bigword-backward-motion",
            "initial_text": "one\n\n two\nthree\n\n\nend\n",
            "key_steps": [b"G", b"B", b"r", b"X"],
        },
        {
            "name": "blank-bigword-forward-motion",
            "initial_text": "one\n\n two\nthree\n\n\nend\n",
            "key_steps": [b"W", b"r", b"X"],
        },
        {
            "name": "eof-ge-motion",
            "initial_text": "one two three four\n",
            "key_steps": [b"W", b"W", b"W", b"2", b"g", b"e", b"r", b"Y"],
        },
        {
            "name": "eof-gE-motion",
            "initial_text": "one,two   three\n",
            "key_steps": [b"W", b"g", b"E", b"r", b"Z"],
        },
        {
            "name": "sentence-forward-line-end-motion",
            "initial_text": "one. two. three.\nalpha. beta. gamma.\n",
            "key_steps": [b"$", b")", b"r", b"Z"],
        },
        {
            "name": "blank-sentence-forward-count-motion",
            "initial_text": "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            "key_steps": [b"2", b")", b"r", b"X"],
        },
        {
            "name": "blank-sentence-backward-motion",
            "initial_text": "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            "key_steps": [b"G", b"(", b"r", b"X"],
        },
        {
            "name": "blank-sentence-forward-change",
            "initial_text": "one two.\n\nalpha beta.\n\nsec\n{\nbody\n}\n",
            "key_steps": [b"c", b")", b"X", b"\x1b"],
        },
        {
            "name": "blank-sentence-backward-yank",
            "initial_text": "one two.\n\nalpha beta.\n\nsec\n{\nbody\n}\n",
            "key_steps": [b"G", b"y", b"(", b"P"],
        },
        {
            "name": "blank-paragraph-forward-count-motion",
            "initial_text": "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            "key_steps": [b"2", b"}", b"r", b"X"],
        },
        {
            "name": "blank-paragraph-backward-count-motion",
            "initial_text": "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            "key_steps": [b"G", b"2", b"{", b"r", b"X"],
        },
        {
            "name": "blank-paragraph-backward-yank",
            "initial_text": "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            "key_steps": [b"G", b"y", b"{", b"P"],
        },
        {
            "name": "blank-sentence-delete-emptyline",
            "initial_text": "\nalpha beta.\n",
            "key_steps": [b"d", b")"],
        },
        {
            "name": "blank-paragraph-delete-emptyline",
            "initial_text": "\nalpha beta gamma delta\n",
            "key_steps": [b"d", b"}"],
        },
        {
            "name": "paragraph-motion-forward-tab-separator",
            "initial_text": "\tone\n\t\n\ttwo\n\t\n\tthree\n",
            "key_steps": [b"}", b"r", b"X"],
        },
        {
            "name": "shift-paragraph-forward-tab-separator",
            "initial_text": "\tone\n\t\n\ttwo\n\t\n\tthree\n",
            "key_steps": [b">", b"}"],
        },
        {
            "name": "unshift-paragraph-forward-tab-separator",
            "initial_text": "\tone\n\t\n\ttwo\n\t\n\tthree\n",
            "key_steps": [b"<", b"}"],
        },
        {
            "name": "short-file-screen-top-motion",
            "initial_text": "line 1\nline 2\nline 3\n",
            "key_steps": [b"H", b"r", b"X"],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "short-file-screen-middle-motion",
            "initial_text": "line 1\nline 2\nline 3\n",
            "key_steps": [b"M", b"r", b"X"],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "short-file-screen-bottom-motion",
            "initial_text": "line 1\nline 2\nline 3\n",
            "key_steps": [b"L", b"r", b"X"],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "short-file-line-scroll-up-motion",
            "initial_text": "line 1\nline 2\nline 3\n",
            "key_steps": [b"2", b"G", b"z", b"\r", b"\x19", b"r", b"X"],
            "rows": 5,
            "cols": 20,
        },
        {
            "name": "mixed-count-line-delete-success",
            "initial_text": "one\ntwo\nthree\nfour\nfive\nsix\n",
            "key_steps": [b"2", b"d", b"2", b"j"],
        },
        {
            "name": "mixed-count-line-delete-noop",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"2", b"d", b"2", b"k"],
        },
        {
            "name": "mixed-count-screen-delete-success",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\n",
            "key_steps": [b"2", b"d", b"M"],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "mixed-count-screen-delete-noop",
            "initial_text": "only\n",
            "key_steps": [b"2", b"d", b"H"],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "mixed-count-word-delete-success",
            "initial_text": "one two three four five\n",
            "key_steps": [b"2", b"d", b"2", b"w"],
        },
        {
            "name": "mixed-count-word-delete-noop",
            "initial_text": "one two three\n",
            "key_steps": [b"2", b"d", b"2", b"b"],
        },
        {
            "name": "mixed-count-sentence-delete-success",
            "initial_text": "one. two. three. four.\nalpha. beta.\n",
            "key_steps": [b"2", b"d", b")"],
        },
        {
            "name": "mixed-count-sentence-delete-noop",
            "initial_text": "one. two. three.\n",
            "key_steps": [b"2", b"d", b"2", b"("],
        },
        {
            "name": "mixed-count-paragraph-delete-success",
            "initial_text": "one\n\ntwo\n\nthree\n\nfour\n\nfive\n",
            "key_steps": [b"2", b"d", b"2", b"}"],
        },
        {
            "name": "mixed-count-paragraph-delete-noop",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b"2", b"d", b"2", b"{"],
        },
        {
            "name": "mixed-count-section-delete-success",
            "initial_text": "intro\n{\nbody1\n}\nmid\n{\nbody2\n}\noutro\n{\nbody3\n}\n",
            "key_steps": [b"2", b"d", b"]", b"["],
        },
        {
            "name": "mixed-count-section-delete-noop",
            "initial_text": "intro\n{\nbody1\n}\nmid\n{\nbody2\n}\noutro\n",
            "key_steps": [b"2", b"d", b"2", b"[", b"["],
        },
        {
            "name": "mixed-count-search-delete-success",
            "initial_text": "aa target bb\ncc target dd\nee target ff\n",
            "key_steps": [b"/", b"target\r", b"w", b"2", b"d", b"n"],
        },
        {
            "name": "mixed-count-search-delete-noop",
            "initial_text": "aa target bb\n",
            "key_steps": [b"/", b"target\r", b"w", b"2", b"d", b"N"],
        },
        {
            "name": "mixed-count-percent-delete-success",
            "initial_text": "if (x) {\n  foo();\n}\nif (y) {\n  bar();\n}\n",
            "key_steps": [b"f", b"{", b"2", b"d", b"%"],
        },
        {
            "name": "mixed-count-percent-delete-noop",
            "initial_text": "plain text\nnext line\n",
            "key_steps": [b"2", b"d", b"%"],
        },
        {
            "name": "search-forward-delete-cross",
            "initial_text": "alpha\nbeta\ngamma\n",
            "key_steps": [b"d", b"/", b"g", b"a", b"m", b"m", b"a", b"\r"],
        },
        {
            "name": "search-forward-change-cross",
            "initial_text": "alpha\nbeta\ngamma\n",
            "key_steps": [b"c", b"/", b"g", b"a", b"m", b"m", b"a", b"\r",
                b"X", b"\x1b"],
        },
        {
            "name": "search-forward-yank-cross",
            "initial_text": "alpha\nbeta\ngamma\n",
            "key_steps": [b"y", b"/", b"g", b"a", b"m", b"m", b"a", b"\r", b"P"],
        },
        {
            "name": "search-backward-delete-cross",
            "initial_text": "alpha\nbeta\ngamma\n",
            "key_steps": [b"G", b"d", b"?", b"a", b"l", b"p", b"h", b"a", b"\r"],
        },
        {
            "name": "search-backward-change-cross",
            "initial_text": "alpha\nbeta\ngamma\n",
            "key_steps": [b"G", b"c", b"?", b"a", b"l", b"p", b"h", b"a", b"\r",
                b"X", b"\x1b"],
        },
        {
            "name": "search-backward-yank-cross",
            "initial_text": "alpha\nbeta\ngamma\n",
            "key_steps": [b"G", b"y", b"?", b"a", b"l", b"p", b"h", b"a", b"\r", b"P"],
        },
        {
            "name": "find-forward-delete",
            "initial_text": "abc def ghi\n",
            "key_steps": [b"0", b"d", b"f", b"g"],
        },
        {
            "name": "find-forward-change",
            "initial_text": "abc def ghi\n",
            "key_steps": [b"0", b"c", b"t", b"g", b"X", b"\x1b"],
        },
        {
            "name": "find-forward-yank",
            "initial_text": "abc def ghi\n",
            "key_steps": [b"0", b"y", b"f", b"g", b"P"],
        },
        {
            "name": "find-backward-delete",
            "initial_text": "abc def ghi\n",
            "key_steps": [b"$", b"d", b"F", b"g"],
        },
        {
            "name": "find-backward-change",
            "initial_text": "abc def ghi\n",
            "key_steps": [b"$", b"c", b"T", b"g", b"X", b"\x1b"],
        },
        {
            "name": "find-backward-yank",
            "initial_text": "abc def ghi\n",
            "key_steps": [b"$", b"y", b"F", b"g", b"P"],
        },
        {
            "name": "match-forward-delete-cross",
            "initial_text": "ab(cd\nef)gh\n",
            "key_steps": [b"0", b"f", b"(", b"d", b"%"],
        },
        {
            "name": "match-forward-change-cross",
            "initial_text": "ab(cd\nef)gh\n",
            "key_steps": [b"0", b"f", b"(", b"c", b"%", b"X", b"\x1b"],
        },
        {
            "name": "match-forward-yank-cross",
            "initial_text": "ab(cd\nef)gh\n",
            "key_steps": [b"0", b"f", b"(", b"y", b"%", b"P"],
        },
        {
            "name": "match-backward-delete-cross",
            "initial_text": "ab(cd\nef)gh\n",
            "key_steps": [b"G", b"f", b")", b"d", b"%"],
        },
        {
            "name": "match-backward-change-cross",
            "initial_text": "ab(cd\nef)gh\n",
            "key_steps": [b"G", b"f", b")", b"c", b"%", b"X", b"\x1b"],
        },
        {
            "name": "match-backward-yank-cross",
            "initial_text": "ab(cd\nef)gh\n",
            "key_steps": [b"G", b"f", b")", b"y", b"%", b"P"],
        },
        {
            "name": "mark-backward-delete-cross",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"j", b"l", b"m", b"a", b"G", b"d", b"`", b"a"],
        },
        {
            "name": "mark-backward-change-cross",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"j", b"l", b"m", b"a", b"G", b"c", b"`", b"a", b"X", b"\x1b"],
        },
        {
            "name": "mark-backward-yank-cross",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"j", b"l", b"m", b"a", b"G", b"y", b"`", b"a", b"P"],
        },
        {
            "name": "mark-forward-delete-cross",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"G", b"l", b"m", b"a", b"g", b"g", b"d", b"`", b"a"],
        },
        {
            "name": "mark-forward-change-cross",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"G", b"l", b"m", b"a", b"g", b"g", b"c", b"`", b"a",
                b"X", b"\x1b"],
        },
        {
            "name": "mark-forward-yank-cross",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"G", b"l", b"m", b"a", b"g", b"g", b"y", b"`", b"a", b"P"],
        },
        {
            "name": "line-minus-yank-put",
            "initial_text": "  one\n  two\n  three\n",
            "key_steps": [b"j", b"j", b"y", b"-", b"P"],
        },
        {
            "name": "screen-top-yank-put",
            "initial_text": "l1\nl2\nl3\nl4\nl5\nl6\n",
            "key_steps": [b"4", b"G", b"y", b"H", b"P"],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "screen-middle-yank-put",
            "initial_text": "l1\nl2\nl3\nl4\nl5\nl6\n",
            "key_steps": [b"5", b"G", b"y", b"M", b"P"],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "shift-dollar-eol",
            "initial_text": "one two\nthree four\n",
            "key_steps": [b">", b"$"],
        },
        {
            "name": "unshift-dollar-eol",
            "initial_text": "\tone two\n\tthree four\n",
            "key_steps": [b"<", b"$"],
        },
        {
            "name": "blank-separator-shift-sentence-noop",
            "initial_text": "one.\n\ntwo.\nthree.\n\nfour.\n",
            "key_steps": [b"j", b">", b")"],
        },
        {
            "name": "change-sentence-backward",
            "initial_text": "One.\nTwo.\nThree.\n",
            "key_steps": [b"G", b"$", b"c", b"(", b"X", b"\x1b"],
        },
        {
            "name": "shift-sentence-backward",
            "initial_text": "One.\nTwo.\nThree.\n",
            "key_steps": [b"G", b">", b"("],
        },
        {
            "name": "unshift-sentence-backward",
            "initial_text": "\tOne.\n\tTwo.\n\tThree.\n",
            "key_steps": [b"G", b"<", b"("],
        },
        {
            "name": "shift-sentence-forward",
            "initial_text": "One.\nTwo.\nThree.\n",
            "key_steps": [b">", b")"],
        },
        {
            "name": "unshift-sentence-forward",
            "initial_text": "\tOne.\n\tTwo.\n\tThree.\n",
            "key_steps": [b"<", b")"],
        },
        {
            "name": "blank-separator-shift-sentence-indented",
            "initial_text": "\tone.\n\n\ttwo.\n\tthree.\n\n\tfour.\n",
            "key_steps": [b"j", b">", b")"],
        },
        {
            "name": "blank-separator-unshift-sentence-indented",
            "initial_text": "\tone.\n\n\ttwo.\n\tthree.\n\n\tfour.\n",
            "key_steps": [b"j", b"<", b")"],
        },
        {
            "name": "blank-separator-delete-paragraph",
            "initial_text": "one\n\ntwo\nthree\n\nfour\n",
            "key_steps": [b"j", b"d", b"}"],
        },
        {
            "name": "blank-separator-change-paragraph",
            "initial_text": "one\n\ntwo\nthree\n\nfour\n",
            "key_steps": [b"j", b"c", b"}", b"X", b"\x1b"],
        },
        {
            "name": "blank-separator-yank-paragraph",
            "initial_text": "one\n\ntwo\nthree\n\nfour\n",
            "key_steps": [b"j", b"y", b"}", b"P"],
        },
        {
            "name": "delete-paragraph-backward",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b"G", b"d", b"{"],
        },
        {
            "name": "change-paragraph-backward",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b"G", b"c", b"{", b"X", b"\x1b"],
        },
        {
            "name": "yank-paragraph-backward",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b"G", b"y", b"{", b"P"],
        },
        {
            "name": "shift-paragraph-backward",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b"G", b">", b"{"],
        },
        {
            "name": "unshift-paragraph-backward",
            "initial_text": "\tone\n\t\n\ttwo\n\t\n\tthree\n",
            "key_steps": [b"G", b"<", b"{"],
        },
        {
            "name": "change-paragraph-forward",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b"c", b"}", b"X", b"\x1b"],
        },
        {
            "name": "yank-paragraph-forward",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b"y", b"}", b"P"],
        },
        {
            "name": "shift-paragraph-forward",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b">", b"}"],
        },
        {
            "name": "unshift-paragraph-forward",
            "initial_text": "\tone\n\t\n\ttwo\n\t\n\tthree\n",
            "key_steps": [b"<", b"}"],
        },
        {
            "name": "change-caret-indent",
            "initial_text": "  one\n  two\n",
            "key_steps": [b"c", b"^", b"X", b"\x1b"],
        },
        {
            "name": "shift-caret-indent",
            "initial_text": "  one\n  two\n",
            "key_steps": [b">", b"^"],
        },
        {
            "name": "unshift-caret-indent",
            "initial_text": "\t  one\n\t  two\n",
            "key_steps": [b"<", b"^"],
        },
        {
            "name": "shift-zero",
            "initial_text": "  one\n  two\n",
            "key_steps": [b">", b"0"],
        },
        {
            "name": "unshift-zero",
            "initial_text": "\t  one\n\t  two\n",
            "key_steps": [b"<", b"0"],
        },
        {
            "name": "shift-pipe",
            "initial_text": "  one\n  two\n",
            "key_steps": [b">", b"1", b"|"],
        },
        {
            "name": "unshift-pipe",
            "initial_text": "\t  one\n\t  two\n",
            "key_steps": [b"<", b"1", b"|"],
        },
        {
            "name": "shift-gg",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"3", b"G", b">", b"g", b"g"],
        },
        {
            "name": "unshift-gg",
            "initial_text": "\tone\n\ttwo\n\tthree\n",
            "key_steps": [b"3", b"G", b"<", b"g", b"g"],
        },
        {
            "name": "shift-G",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b">", b"G"],
        },
        {
            "name": "unshift-G",
            "initial_text": "\tone\n\ttwo\n\tthree\n",
            "key_steps": [b"<", b"G"],
        },
        {
            "name": "shift-plus-line",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b">", b"+"],
        },
        {
            "name": "unshift-plus-line",
            "initial_text": "\tone\n\ttwo\n\tthree\n",
            "key_steps": [b"<", b"+"],
        },
        {
            "name": "shift-enter-line",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b">", b"\r"],
        },
        {
            "name": "unshift-enter-line",
            "initial_text": "\tone\n\ttwo\n\tthree\n",
            "key_steps": [b"<", b"\r"],
        },
        {
            "name": "delete-plus-line",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"d", b"+"],
        },
        {
            "name": "change-enter-line",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"c", b"\r", b"X", b"\x1b"],
        },
        {
            "name": "yank-minus-line",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"G", b"y", b"-", b"P"],
        },
        {
            "name": "delete-enter-line",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"d", b"\r"],
        },
        {
            "name": "yank-enter-line",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"y", b"\r", b"P"],
        },
        {
            "name": "change-minus-line",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"j", b"j", b"c", b"-", b"X", b"\x1b"],
        },
        {
            "name": "shift-underscore-line",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b">", b"_"],
        },
        {
            "name": "unshift-underscore-line",
            "initial_text": "\tone\n\ttwo\n\tthree\n",
            "key_steps": [b"<", b"_"],
        },
        {
            "name": "shift-H",
            "initial_text": "one\ntwo\nthree\nfour\nfive\n",
            "key_steps": [b"4", b"G", b">", b"H"],
            "rows": 5,
            "cols": 20,
        },
        {
            "name": "unshift-H",
            "initial_text": "\tone\n\ttwo\n\tthree\n\tfour\n\tfive\n",
            "key_steps": [b"4", b"G", b"<", b"H"],
            "rows": 5,
            "cols": 20,
        },
        {
            "name": "shift-M",
            "initial_text": "one\ntwo\nthree\nfour\nfive\n",
            "key_steps": [b"2", b"G", b">", b"M"],
            "rows": 5,
            "cols": 20,
        },
        {
            "name": "unshift-M",
            "initial_text": "\tone\n\ttwo\n\tthree\n\tfour\n\tfive\n",
            "key_steps": [b"2", b"G", b"<", b"M"],
            "rows": 5,
            "cols": 20,
        },
        {
            "name": "shift-L",
            "initial_text": "one\ntwo\nthree\nfour\nfive\n",
            "key_steps": [b">", b"L"],
            "rows": 5,
            "cols": 20,
        },
        {
            "name": "unshift-L",
            "initial_text": "\tone\n\ttwo\n\tthree\n\tfour\n\tfive\n",
            "key_steps": [b"<", b"L"],
            "rows": 5,
            "cols": 20,
        },
        {
            "name": "delete-screen-middle",
            "initial_text": "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
            "key_steps": [b"j", b"j", b"d", b"M"],
            "rows": 7,
        },
        {
            "name": "change-screen-top",
            "initial_text": "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
            "key_steps": [b"j", b"j", b"j", b"c", b"H", b"X", b"\x1b"],
            "rows": 6,
        },
        {
            "name": "change-screen-bottom",
            "initial_text": "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
            "key_steps": [b"c", b"L", b"X", b"\x1b"],
            "rows": 6,
        },
        {
            "name": "delete-screen-bottom",
            "initial_text": "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
            "key_steps": [b"d", b"L"],
            "rows": 6,
        },
        {
            "name": "line-plus-unshift-tab-indent",
            "initial_text": "\t  one\n\t  two\n\t  three\n",
            "key_steps": [b"<", b"+"],
        },
        {
            "name": "shift-e",
            "initial_text": "one two\nthree four\n",
            "key_steps": [b">", b"e"],
        },
        {
            "name": "unshift-w",
            "initial_text": "\tone two\n\tthree four\n",
            "key_steps": [b"<", b"w"],
        },
        {
            "name": "unshift-W",
            "initial_text": "\tone,two\n\tthree four\n",
            "key_steps": [b"<", b"W"],
        },
        {
            "name": "unshift-e",
            "initial_text": "\tone two\n\tthree four\n",
            "key_steps": [b"<", b"e"],
        },
        {
            "name": "unshift-E",
            "initial_text": "\tone,two\n\tthree four\n",
            "key_steps": [b"<", b"E"],
        },
        {
            "name": "shift-b",
            "initial_text": "one two\nthree four\n",
            "key_steps": [b"G", b">", b"b"],
        },
        {
            "name": "shift-B",
            "initial_text": "one,two\nthree,four\n",
            "key_steps": [b"G", b">", b"B"],
        },
        {
            "name": "shift-ge",
            "initial_text": "one two\nthree four\n",
            "key_steps": [b"G", b">", b"g", b"e"],
        },
        {
            "name": "shift-gE",
            "initial_text": "one,two\nthree,four\n",
            "key_steps": [b"G", b">", b"g", b"E"],
        },
        {
            "name": "unshift-ge",
            "initial_text": "\tone two\n\tthree four\n",
            "key_steps": [b"G", b"<", b"g", b"e"],
        },
        {
            "name": "delete-word-forward",
            "initial_text": "one two three\n",
            "key_steps": [b"d", b"w"],
        },
        {
            "name": "delete-bigword-forward",
            "initial_text": "one,two   three\n",
            "key_steps": [b"d", b"W"],
        },
        {
            "name": "yank-word-forward",
            "initial_text": "one two three\n",
            "key_steps": [b"y", b"w", b"P"],
        },
        {
            "name": "yank-bigword-forward",
            "initial_text": "one,two   three\n",
            "key_steps": [b"y", b"W", b"P"],
        },
        {
            "name": "delete-word-end",
            "initial_text": "one two three\n",
            "key_steps": [b"d", b"e"],
        },
        {
            "name": "change-word-end",
            "initial_text": "one two three\n",
            "key_steps": [b"c", b"e", b"X", b"\x1b"],
        },
        {
            "name": "yank-word-end",
            "initial_text": "one two three\n",
            "key_steps": [b"y", b"e", b"P"],
        },
        {
            "name": "delete-bigword-end",
            "initial_text": "one,two   three\n",
            "key_steps": [b"d", b"E"],
        },
        {
            "name": "yank-bigword-end",
            "initial_text": "one,two   three\n",
            "key_steps": [b"y", b"E", b"P"],
        },
        {
            "name": "change-bigword-end",
            "initial_text": "one,two   three\n",
            "key_steps": [b"c", b"E", b"X", b"\x1b"],
        },
        {
            "name": "word-search-repeat-change",
            "initial_text": "one target\ntwo target\nthree target\n",
            "key_steps": [b"w", b"*", b"c", b"*", b"X", b"\x1b", b"n", b"."],
        },
        {
            "name": "backward-word-repeat-change",
            "initial_text": "one two three four\nalpha beta gamma\n",
            "key_steps": [b"$", b"c", b"b", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "big-backward-word-repeat-change",
            "initial_text": "one two three four\nalpha beta gamma\n",
            "key_steps": [b"$", b"c", b"B", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "screen-middle-repeat-change",
            "initial_text": "one\ntwo\nthree\nfour\nfive\n",
            "key_steps": [b"2", b"G", b"c", b"M", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "screen-bottom-repeat-change",
            "initial_text": "line 1\nline 2\nline 3\nline 4\nline 5\nline 6\nline 7\n",
            "key_steps": [b"2", b"G", b"c", b"L", b"X", b"\x1b", b"j", b"."],
            "rows": 6,
            "cols": 20,
        },
        {
            "name": "find-change-repeat-forward",
            "initial_text": "alpha beta gamma\nalpha beta gamma\n",
            "key_steps": [b"0", b"c", b"f", b"g", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "find-change-repeat-till",
            "initial_text": "alpha beta gamma\nalpha beta gamma\n",
            "key_steps": [b"0", b"c", b"t", b"g", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "find-change-repeat-backward",
            "initial_text": "alpha beta gamma\nalpha beta gamma\n",
            "key_steps": [b"$", b"c", b"F", b"b", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "find-change-repeat-backward-till",
            "initial_text": "alpha beta gamma\nalpha beta gamma\n",
            "key_steps": [b"$", b"c", b"T", b"b", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "repeat-find-change",
            "initial_text": "alpha beta gamma\nalpha beta gamma\n",
            "key_steps": [b"0", b"c", b"t", b"g", b"X", b"\x1b", b"j", b";", b"."],
        },
        {
            "name": "repeat-find-reverse-change",
            "initial_text": "alpha beta gamma\nalpha beta gamma\n",
            "key_steps": [b"$", b"c", b"T", b"b", b"X", b"\x1b", b"j", b",", b"."],
        },
        {
            "name": "insert-repeat-change",
            "initial_text": "one\ntwo\n",
            "key_steps": [b"i", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "append-repeat-change",
            "initial_text": "one\ntwo\n",
            "key_steps": [b"a", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "insert-first-nonblank-repeat-change",
            "initial_text": "  one\n  two\n",
            "key_steps": [b"I", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "append-eol-repeat-change",
            "initial_text": "one\ntwo\n",
            "key_steps": [b"A", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "open-below-repeat-change",
            "initial_text": "one\ntwo\n",
            "key_steps": [b"o", b"X", b"\x1b", b"k", b"j", b"."],
        },
        {
            "name": "open-above-repeat-change",
            "initial_text": "one\ntwo\n",
            "key_steps": [b"O", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "replace-repeat-change",
            "initial_text": "abc\ndef\n",
            "key_steps": [b"R", b"X", b"Y", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-word-repeat-change",
            "initial_text": "alpha beta\ngamma beta\n",
            "key_steps": [b"c", b"w", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-bigword-repeat-change",
            "initial_text": "alpha,beta\ngamma,beta\n",
            "key_steps": [b"c", b"W", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "substitute-repeat-change",
            "initial_text": "ab\ncd\n",
            "key_steps": [b"s", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-to-eol-repeat-change",
            "initial_text": "alpha beta\ngamma beta\n",
            "key_steps": [b"C", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "substitute-line-repeat-change",
            "initial_text": "alpha\nbeta\n",
            "key_steps": [b"S", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-line-repeat-change",
            "initial_text": "one\ntwo\n",
            "key_steps": [b"c", b"c", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-char-repeat-change",
            "initial_text": "ab\ncd\n",
            "key_steps": [b"c", b"l", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-back-char-repeat-change",
            "initial_text": "ab\ncd\n",
            "key_steps": [b"l", b"c", b"h", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-dollar-repeat-change",
            "initial_text": "alpha beta\ngamma beta\n",
            "key_steps": [b"c", b"$", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-word-end-repeat-change",
            "initial_text": "alpha beta\ngamma beta\n",
            "key_steps": [b"c", b"e", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-bigword-end-repeat-change",
            "initial_text": "alpha,beta\ngamma,beta\n",
            "key_steps": [b"c", b"E", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-to-col0-repeat-change",
            "initial_text": "ab cd\nef gh\n",
            "key_steps": [b"l", b"l", b"c", b"0", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "change-to-first-nonblank-repeat-change",
            "initial_text": "  ab\n  cd\n",
            "key_steps": [b"l", b"l", b"c", b"^", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "paragraph-forward-repeat-change",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b"c", b"}", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "sentence-forward-repeat-change",
            "initial_text": "one. two. three.\nalpha. beta. gamma.\n",
            "key_steps": [b"$", b"c", b")", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "search-forward-repeat-change",
            "initial_text": "one target\nline two\ntarget three\n",
            "key_steps": [b"c", b"/", b"t", b"a", b"r", b"g", b"e", b"t", b"\r",
                b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "search-backward-repeat-change",
            "initial_text": "one target\nline two\ntarget three\n",
            "key_steps": [b"G", b"c", b"?", b"t", b"a", b"r", b"g", b"e", b"t", b"\r",
                b"X", b"\x1b", b"k", b"."],
        },
        {
            "name": "search-repeat-change-n",
            "initial_text": "one target\ntwo target\nthree target\n",
            "key_steps": [b"/", b"target", b"\r", b"c", b"n", b"X", b"\x1b", b"n", b"."],
        },
        {
            "name": "search-repeat-change-N",
            "initial_text": "one target\ntwo target\nthree target\n",
            "key_steps": [b"G", b"?", b"target", b"\r", b"c", b"N", b"X", b"\x1b", b"N", b"."],
        },
        {
            "name": "word-search-repeat-change-hash",
            "initial_text": "one target\ntwo target\nthree target\n",
            "key_steps": [b"G", b"w", b"#", b"c", b"#", b"X", b"\x1b", b"N", b"."],
        },
        {
            "name": "paragraph-repeat-change",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b"G", b"c", b"{", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "match-repeat-change",
            "initial_text": "if (x) {\n  foo();\n}\nif (y) {\n  bar();\n}\n",
            "key_steps": [b"f", b"{", b"c", b"%", b"X", b"\x1b", b"j", b"j", b"j", b"f", b"{", b"."],
        },
        {
            "name": "mark-line-repeat-change",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"j", b"m", b"a", b"j", b"c", b"'", b"a", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "mark-exact-repeat-change",
            "initial_text": "ab\ncd\nef\n",
            "key_steps": [b"l", b"m", b"a", b"j", b"l", b"c", b"`", b"a", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "line-plus-repeat-change",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"c", b"+", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "line-minus-repeat-change",
            "initial_text": "one\ntwo\nthree\nfour\n",
            "key_steps": [b"j", b"c", b"-", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "line-underscore-repeat-change",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"c", b"_", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "screen-top-repeat-change",
            "initial_text": "one\ntwo\nthree\nfour\nfive\n",
            "key_steps": [b"3", b"G", b"c", b"H", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "backward-end-repeat-change",
            "initial_text": "one two three four\nalpha beta gamma\n",
            "key_steps": [b"$", b"c", b"g", b"e", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "backward-big-end-repeat-change",
            "initial_text": "one two three four\nalpha beta gamma\n",
            "key_steps": [b"$", b"c", b"g", b"E", b"X", b"\x1b", b"j", b"."],
        },
        {
            "name": "repeat-find-semicolon-change",
            "initial_text": "alpha beta gamma\nalpha beta gamma\n",
            "key_steps": [b"0", b"c", b"t", b"g", b"X", b"\x1b", b"j", b";", b"."],
        },
        {
            "name": "repeat-find-comma-change",
            "initial_text": "alpha beta gamma\nalpha beta gamma\n",
            "key_steps": [b"$", b"c", b"T", b"b", b"X", b"\x1b", b"j", b",", b"."],
        },
        {
            "name": "multi-undo-redo-insert",
            "initial_text": "abc\n",
            "key_steps": [b"i", b"X", b"\x1b", b"A", b"Z", b"\x1b", b"u", b"u",
                b"\x12", b"\x12"],
        },
        {
            "name": "multi-undo-join",
            "initial_text": "one\ntwo\nthree\n",
            "key_steps": [b"J", b"J", b"u", b"u"],
        },
        {
            "name": "multi-undo-change",
            "initial_text": "alpha beta gamma\n",
            "key_steps": [b"c", b"w", b"X", b"\x1b", b"0", b"w", b"c", b"w",
                b"Y", b"\x1b", b"u", b"u"],
        },
        {
            "name": "undo-boundary-insert-session",
            "initial_text": "abc\n",
            "key_steps": [b"i", b"X", b"Y", b"\x1b", b"u"],
        },
        {
            "name": "undo-boundary-replace-session",
            "initial_text": "abc\n",
            "key_steps": [b"R", b"X", b"Y", b"\x1b", b"u"],
        },
        {
            "name": "undo-boundary-open-line",
            "initial_text": "one\n",
            "key_steps": [b"o", b"A", b"B", b"\x1b", b"u"],
        },
        {
            "name": "undo-boundary-dot-replay",
            "initial_text": "one two\nthree four\n",
            "key_steps": [b"c", b"w", b"X", b"\x1b", b"j", b".", b"u"],
        },
        {
            "name": "undo-boundary-ex-command",
            "initial_text": "one\ntwo\n",
            "key_steps": [b":", b"1", b",", b"2", b"s", b"/o/O/g\r", b"u"],
        },
        {
            "name": "redo-invalidation-after-edit",
            "initial_text": "abc\n",
            "key_steps": [b"i", b"X", b"\x1b", b"A", b"Y", b"\x1b", b"u", b"a",
                b"Z", b"\x1b", b"\x12"],
        },
        {
            "name": "redo-replays-transaction-boundaries",
            "initial_text": "abc\n",
            "key_steps": [b"i", b"X", b"\x1b", b"A", b"Y", b"\x1b", b"u", b"u",
                b"\x12", b"\x12"],
        },
    ]

    if args.start_at:
        start_index = next(
            (index for index, case in enumerate(cases)
             if case["name"] == args.start_at),
            None,
        )
        if start_index is None:
            print(f"unknown oracle case: {args.start_at}", file=sys.stderr)
            return 2
        cases = cases[start_index:]

    if args.match:
        cases = [
            case for case in cases
            if any(pattern in case["name"] for pattern in args.match)
        ]

    if args.limit is not None:
        cases = cases[:args.limit]

    if args.list:
        for case in cases:
            print(case["name"])
        return 0

    if not cases:
        print("SKIP: no oracle cases selected")
        return 0

    vim_path = shutil.which("vim")
    if not vim_path:
        print("SKIP: vim not found")
        return 0

    helpers = load_pty_helpers()

    for case in cases:
        compare_case(
            helpers,
            vi_path,
            vim_path,
            case["name"],
            case["initial_text"],
            case["key_steps"],
            rows=case.get("rows", 24),
            cols=case.get("cols", 80),
            final_keys=case.get("final_keys", b":wq\r"),
            extra_files=case.get("extra_files"),
            file_args=case.get("file_args"),
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
