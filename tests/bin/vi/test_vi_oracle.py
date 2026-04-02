#!/usr/bin/env python3

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
    print(f"PASS: {name}")


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/vi", file=sys.stderr)
        return 2

    vi_path = sys.argv[1]
    vim_path = shutil.which("vim")
    if not vim_path:
        print("SKIP: vim not found")
        return 0

    helpers = load_pty_helpers()
    cases = [
        {
            "name": "search-column-repeat",
            "initial_text": "one two three\nalpha two beta\n",
            "key_steps": [b"/", b"two\r", b"r", b"Z", b"n", b"r", b"Y"],
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
            "name": "screen-middle-repeat-change",
            "initial_text": "one\ntwo\nthree\nfour\nfive\n",
            "key_steps": [b"2", b"G", b"c", b"M", b"X", b"\x1b", b"j", b"."],
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
            "name": "paragraph-forward-repeat-change",
            "initial_text": "one\n\ntwo\n\nthree\n",
            "key_steps": [b"c", b"}", b"X", b"\x1b", b"j", b"."],
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
    ]

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
