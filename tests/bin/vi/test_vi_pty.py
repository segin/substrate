#!/usr/bin/env python3

import fcntl
import os
import pty
import select
import struct
import sys
import tempfile
import time
import termios


def read_some(master_fd, timeout):
    out = bytearray()
    end = time.time() + timeout
    while time.time() < end:
        ready, _, _ = select.select([master_fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(master_fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        out.extend(chunk)
    return bytes(out)


def require(cond, msg):
    if not cond:
        print(f"FAIL: {msg}", file=sys.stderr)
        raise SystemExit(1)


def send_keys(master_fd, output, data, timeout=0.2):
    os.write(master_fd, data)
    output += read_some(master_fd, timeout)
    return output


def run_vi_session(vi_path, initial_text, key_steps, final_timeout=0.3, rows=24,
                   cols=80, final_keys=b":wq\r"):
    fd, temp_path = tempfile.mkstemp(prefix="exvi-", text=True)
    os.write(fd, initial_text.encode("utf-8"))
    os.close(fd)

    pid, master_fd = pty.fork()
    if pid == 0:
        os.execv(vi_path, [vi_path, temp_path])

    fcntl.ioctl(master_fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    output = read_some(master_fd, 0.4)
    for step in key_steps:
        if isinstance(step, tuple) and step and step[0] == "winsize":
            _, step_rows, step_cols, *rest = step
            step_timeout = rest[0] if rest else 0.3

            fcntl.ioctl(master_fd, termios.TIOCSWINSZ,
                        struct.pack("HHHH", step_rows, step_cols, 0, 0))
            output += read_some(master_fd, step_timeout)
            continue
        output = send_keys(master_fd, output, step)
    if final_keys is not None:
        output = send_keys(master_fd, output, final_keys[:1])
        output = send_keys(master_fd, output, final_keys[1:], final_timeout)
    else:
        output += read_some(master_fd, final_timeout)

    _, status = os.waitpid(pid, 0)
    os.close(master_fd)

    with open(temp_path, "r", encoding="utf-8") as f:
        saved = f.read()
    os.unlink(temp_path)

    return os.waitstatus_to_exitcode(status), output.decode("latin1", "replace"), saved


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/vi", file=sys.stderr)
        return 2

    vi_path = sys.argv[1]
    exit_code, decoded, saved = run_vi_session(vi_path, "one\ntwo\nthree\ntwo again\nfive\n", [
        b"G", b"g", b"g", b"/", b"two\r", b"n", b"N", b"?", b"one\r", b"g", b"g",
        b"i", b"X", b"\x1b", b"j", b"x", b"j", b"a", b"!", b"\x1b", b"u",
        b"O", b"T", b"o", b"p", b"\x1b", b"o", b"t", b"a", b"i", b"l", b"\x1b",
        b"g", b"g", b"$", b"a", b"-", b"s", b"p", b"l", b"i", b"t", b"\r",
        b"l", b"i", b"n", b"e", b"\x1b", b"w", b"e", b"b", b"y", b"y", b"p",
        b"P", b"d", b"d", b"/", b"wo\r", b"c", b"w", b"T", b"W", b"O", b"\x1b",
        b"/", b"two again\r", b"w", b"e", b"b", b"D", b"/", b"tail\r", b"C",
        b"T", b"A", b"I", b"L", b"1", b"\x1b", b"/", b"three\r", b"s", b"T",
        b"H", b"\x1b", b"/", b"two \r", b"R", b"T", b"w", b"o", b"!", b"\x1b",
        b"/", b"five\r", b"r", b"F", b"/", b"THhree\r", b"A", b"!", b"\x1b",
        b"/", b"Two!\r", b"I", b">", b"\x1b", b"/", b"Five\r", b"S", b"F",
        b"i", b"n", b"a", b"l", b"\x1b", b"/", b"Xone\r", b"J", b"/", b"tail\r",
        b"d", b"w", b"/", b"Final\r", b"c", b"c", b"D", b"o", b"n", b"e",
        b"\x1b", b"/", b"Xone\r", b"3", b"J", b"3", b"G", b"2", b"x", b".",
        b"g", b"g", b"l", b"l", b"^", b"\x06", b"H", b"M", b"L",
    ])
    require(exit_code == 0, f"vi exited with status {exit_code}")
    require("\x1b[" in decoded, "missing ANSI screen output")
    require("one" in decoded, "missing initial buffer content")
    require("line 5/5" in decoded, "missing G navigation status")
    require("line 1/5" in decoded, "missing gg navigation status")
    require("line 2/5" in decoded, "missing forward search status")
    require("line 4/5" in decoded, "missing repeat search status")
    require("line 3/6" in decoded, "missing count-based G navigation status")
    require("line 6/6" in decoded, "missing page-scroll status")
    require("-- INSERT --" in decoded, "missing insert mode status")
    require("-- REPLACE --" in decoded, "missing replace mode status")
    require(":wq" in decoded, "missing ex command prompt rendering")
    require("\r\n\x1b[K~" in decoded, "missing CRLF ladder rendering for screen filler")
    require(saved == "Top-split\nline\n1\n\nXone TWO THhree! >Two!\nDone\n",
            f"unexpected saved buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(vi_path, "alpha beta gamma\nsecond line here\n", [
        b"w",
        b"d", b"e",
        b"w",
        b"c", b"$", b"D", b"O", b"N", b"E", b"\x1b",
        b"j",
        b"0", b"w",
        b"d", b"$",
    ])
    require(exit_code == 0, f"operator vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing operator insert status")
    require("line 2/2" in decoded, "missing operator second-line status")
    require(saved == "alpha  DONE\nsecond \n",
            f"unexpected operator-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha,beta   gamma delta\n",
        [b"E", b"r", b"4", b"0", b"W", b"r", b"1", b"$", b"B", b"r", b"3"],
    )
    require(exit_code == 0, f"bigword-motion vi exited with status {exit_code}")
    require("line 1/1" in decoded, "missing bigword-motion status")
    require(saved == "alpha,bet4   1amma 3elta\n",
            f"unexpected bigword-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha,beta   gamma\n",
        [b"d", b"W"],
    )
    require(exit_code == 0, f"bigword-operator vi exited with status {exit_code}")
    require(saved == "gamma\n",
            f"unexpected bigword-operator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b'"', b'a', b'y', b'y', b'j', b'"', b'a', b'p'],
    )
    require(exit_code == 0, f"named-yank-put vi exited with status {exit_code}")
    require(saved == "one\ntwo\none\nthree\n",
            f"unexpected named-yank-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b'j', b'"', b'b', b'd', b'd', b'g', b'g', b'"', b'b', b'P'],
    )
    require(exit_code == 0, f"named-delete-put vi exited with status {exit_code}")
    require(saved == "two\none\nthree\n",
            f"unexpected named-delete-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b'x', b'p'],
    )
    require(exit_code == 0, f"char-put vi exited with status {exit_code}")
    require(saved == "bacd\n",
            f"unexpected char-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b"x", b"u", b"\x12"],
    )
    require(exit_code == 0, f"redo vi exited with status {exit_code}")
    require(saved == "bcd\n",
            f"unexpected redo buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b'd', b'w', b'P'],
    )
    require(exit_code == 0, f"char-operator-put vi exited with status {exit_code}")
    require(saved == "alpha beta\n",
            f"unexpected char-operator-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b'y', b'w', b'$', b'p'],
    )
    require(exit_code == 0, f"char-yank-put vi exited with status {exit_code}")
    require(saved == "alpha betaalpha \n",
            f"unexpected char-yank-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b'"', b'a', b'x', b'l', b'"', b'a', b'p'],
    )
    require(exit_code == 0, f"named-char-put vi exited with status {exit_code}")
    require(saved == "bcad\n",
            f"unexpected named-char-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b'"', b'a', b'y', b'w', b'$', b'"', b'a', b'p'],
    )
    require(exit_code == 0, f"named-char-yank vi exited with status {exit_code}")
    require(saved == "abcdabcd\n",
            f"unexpected named-char-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b'y', b'l', b'$', b'p'],
    )
    require(exit_code == 0, f"char-hl-yank vi exited with status {exit_code}")
    require(saved == "abcda\n",
            f"unexpected char-hl-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b'l', b'l', b'd', b'h'],
    )
    require(exit_code == 0, f"char-hl-delete vi exited with status {exit_code}")
    require(saved == "acd\n",
            f"unexpected char-hl-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"X", b"\t", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"insert-tab vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-tab insert status")
    require(saved == "X\tYabc\n",
            f"unexpected insert-tab buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b"i", b"X", b"\x1b", b"Z", b"Z"],
        final_keys=None,
    )
    require(exit_code == 0, f"ZZ vi exited with status {exit_code}")
    require(saved == "Xone\n",
            f"unexpected ZZ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b"i", b"X", b"\x1b", b"Z", b"Q"],
        final_keys=None,
    )
    require(exit_code == 0, f"ZQ vi exited with status {exit_code}")
    require(saved == "one\n",
            f"unexpected ZQ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  abc\n",
        [b"I", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"I-first-nonblank vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing I-first-nonblank insert status")
    require(saved == "  Xabc\n",
            f"unexpected I-first-nonblank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "",
        [b"i", b"X", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"empty-insert vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing empty-insert insert status")
    require(saved == "XY\n",
            f"unexpected empty-insert buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "",
        [b"i", b"\r", b"\x1b"],
    )
    require(exit_code == 0, f"empty-enter vi exited with status {exit_code}")
    require("line 1/1" in decoded, "missing empty-enter initial line status")
    require("-- INSERT --" in decoded, "missing empty-enter insert status")
    require(saved == "\n",
            f"unexpected empty-enter buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"X", b"\x1b[A", b"\x1b[B", b"\x1b[C", b"\x1b[D", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"insert-arrow vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-arrow insert status")
    require(saved == "XYabc\n",
            f"unexpected insert-arrow buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"i", b"X", b"\x1b[H", b"Y", b"\x1b[F", b"Z", b"\x1b"],
    )
    require(exit_code == 0, f"insert-home-end vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-home-end insert status")
    require(saved == "YXabcdeZ\n",
            f"unexpected insert-home-end buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"i", b"\x1b[C", b"\x1b[C", b"\x1b[3~", b"\x1b"],
    )
    require(exit_code == 0, f"insert-delete vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-delete insert status")
    require(saved == "abde\n",
            f"unexpected insert-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"j", b"i", b"\x7f", b"\x1b"],
    )
    require(exit_code == 0, f"insert-backspace-join vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-backspace-join insert status")
    require(saved == "onetwo\n",
            f"unexpected insert-backspace-join buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"i", b"\x1b[1;5C", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-right vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-right insert status")
    require(saved == "one Xtwo three\n",
            f"unexpected insert-ctrl-right buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"A", b"\x1b[1;5D", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-left vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-left insert status")
    require(saved == "one two Xthree\n",
            f"unexpected insert-ctrl-left buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"A", b"\x17", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-w vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-w insert status")
    require(saved == "one two X\n",
            f"unexpected insert-ctrl-w buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [b"2", b"5", b"l", b"r", b"Z"],
        rows=8,
        cols=20,
    )
    require(exit_code == 0, f"long-line vi exited with status {exit_code}")
    require("KLMNO" in decoded, "missing horizontally scrolled long-line content")
    require(saved == "0123456789abcdefghijKLMNOZQRST\n",
            f"unexpected long-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [("winsize", 8, 28, 0.5)],
        rows=8,
        cols=16,
        final_keys=b":q\r",
    )
    require(exit_code == 0, f"resize vi exited with status {exit_code}")
    require("0123456789abcdefghijKLMNOPQ" in decoded,
            "missing immediate wide-line repaint after resize")
    require(saved == "0123456789abcdefghijKLMNOPQRST\n",
            f"unexpected resize buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\tb\n",
        [],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"tab-render vi exited with status {exit_code}")
    require("a       b" in decoded, "missing expanded tab rendering")
    require("^I" not in decoded, "rendered tabs as caret escapes in normal visual mode")
    require(saved == "a\tb\n",
            f"unexpected tab-render buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\tb\n",
        [b":", b"set ts=4\r"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"tab-render-set vi exited with status {exit_code}")
    require("a   b" in decoded, "missing tabstop=4 rendering")
    require(saved == "a\tb\n",
            f"unexpected tab-render-set buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [b"3", b"0", b"|", b"r", b"Z"],
        rows=8,
        cols=20,
    )
    require(exit_code == 0, f"column-motion vi exited with status {exit_code}")
    require("NOPQRSZ" in decoded, "missing far-right long-line content")
    require(saved == "0123456789abcdefghijKLMNOPQRSZ\n",
            f"unexpected column-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\tbcdefghijklmnopqrstuvwxyz\n",
        [b"2", b"5", b"|", b"r", b"Z"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"tab-column-motion vi exited with status {exit_code}")
    require("bcdefghijklmnopqZ" in decoded, "missing tab-aware far-right long-line content")
    require(saved == "a\tbcdefghijklmnopqZstuvwxyz\n",
            f"unexpected tab-column-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [b"d", b"3", b"0", b"|"],
        rows=8,
        cols=20,
    )
    require(exit_code == 0, f"column-delete vi exited with status {exit_code}")
    require(saved == "T\n",
            f"unexpected column-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\tbcdefghijklmnopqrstuvwxyz\n",
        [b"c", b"2", b"5", b"|", b"X", b"\x1b"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"tab-column-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing tab-column change insert status")
    require(saved == "Xrstuvwxyz\n",
            f"unexpected tab-column-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc   \ndef   \n",
        [b"g", b"_", b"r", b"Z", b"2", b"g", b"_", b"r", b"Y"],
    )
    require(exit_code == 0, f"g_-motion vi exited with status {exit_code}")
    require("line 2/2" in decoded, "missing counted g_ status")
    require(saved == "abZ   \ndeY   \n",
            f"unexpected g_-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\n  bb\ncc\n",
        [b"2", b"_", b"r", b"X"],
    )
    require(exit_code == 0, f"underscore-motion vi exited with status {exit_code}")
    require("line 2/3" in decoded, "missing underscore-motion status")
    require(saved == "aa\n  Xb\ncc\n",
            f"unexpected underscore-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"d", b"_"],
    )
    require(exit_code == 0, f"d_ vi exited with status {exit_code}")
    require(saved == "two\nthree\n",
            f"unexpected d_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"c", b"_", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c_ vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c_ insert status")
    require(saved == "X\ntwo\nthree\n",
            f"unexpected c_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"y", b"_", b"P"],
    )
    require(exit_code == 0, f"y_ vi exited with status {exit_code}")
    require(saved == "one\none\ntwo\n",
            f"unexpected y_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"d", b"j"],
    )
    require(exit_code == 0, f"dj vi exited with status {exit_code}")
    require(saved == "three\nfour\n",
            f"unexpected dj buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"y", b"k", b"P"],
    )
    require(exit_code == 0, f"yk vi exited with status {exit_code}")
    require(saved == "one\ntwo\ntwo\nthree\nthree\nfour\n",
            f"unexpected yk buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"c", b"+", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c+ vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c+ insert status")
    require(saved == "X\nthree\nfour\n",
            f"unexpected c+ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"d", b"-"],
    )
    require(exit_code == 0, f"d- vi exited with status {exit_code}")
    require(saved == "one\nfour\n",
            f"unexpected d- buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"d", b"G"],
    )
    require(exit_code == 0, f"dG vi exited with status {exit_code}")
    require(saved == "",
            f"unexpected dG buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"3", b"y", b"G", b"P"],
    )
    require(exit_code == 0, f"3yG vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\none\ntwo\nthree\nfour\n",
            f"unexpected 3yG buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"c", b"G", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cG vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cG insert status")
    require(saved == "one\nX\n",
            f"unexpected cG buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"d", b"g", b"g"],
    )
    require(exit_code == 0, f"dgg vi exited with status {exit_code}")
    require(saved == "four\n",
            f"unexpected dgg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"y", b"g", b"g", b"P"],
    )
    require(exit_code == 0, f"ygg vi exited with status {exit_code}")
    require(saved == "one\ntwo\none\ntwo\nthree\nthree\nfour\n",
            f"unexpected ygg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"c", b"g", b"g", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cgg vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cgg insert status")
    require(saved == "X\nfour\n",
            f"unexpected cgg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"j", b"j", b"j", b"d", b"H"],
        rows=6,
    )
    require(exit_code == 0, f"dH vi exited with status {exit_code}")
    require(saved == "five\nsix\nseven\neight\n",
            f"unexpected dH buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"j", b"c", b"M", b"X", b"\x1b"],
        rows=7,
    )
    require(exit_code == 0, f"cM vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cM insert status")
    require(saved == "one\nX\nfour\nfive\nsix\nseven\neight\n",
            f"unexpected cM buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"j", b"2", b"y", b"L", b"P"],
        rows=6,
    )
    require(exit_code == 0, f"2yL vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\nfour\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
            f"unexpected 2yL buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"3", b"L", b"r", b"Z", b"2", b"H", b"r", b"Y"],
        rows=6,
    )
    require(exit_code == 0, f"counted-HL vi exited with status {exit_code}")
    require(saved == "one\nYwo\nZhree\nfour\nfive\nsix\nseven\neight\n",
            f"unexpected counted-HL buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b"l", b"d", b"g", b"_"],
    )
    require(exit_code == 0, f"dg_ vi exited with status {exit_code}")
    require(saved == "a\n",
            f"unexpected dg_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b"l", b"c", b"g", b"_", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cg_ vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cg_ insert status")
    require(saved == "aX\n",
            f"unexpected cg_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b"l", b"y", b"g", b"_", b"P"],
    )
    require(exit_code == 0, f"yg_ vi exited with status {exit_code}")
    require(saved == "abcdbcd\n",
            f"unexpected yg_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "One. Two. Three.\n",
        [b"d", b")"],
    )
    require(exit_code == 0, f"d) vi exited with status {exit_code}")
    require(saved == "Two. Three.\n",
            f"unexpected d) buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "One. Two. Three.\n",
        [b"c", b")", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c) vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c) insert status")
    require(saved == "XTwo. Three.\n",
            f"unexpected c) buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\nthree\n\nfour\n",
        [b"d", b"}"],
    )
    require(exit_code == 0, f"d}} vi exited with status {exit_code}")
    require(saved == "\ntwo\nthree\n\nfour\n",
            f"unexpected d}} buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"d", b"/", b"g", b"a", b"m", b"m", b"a", b"\r"],
    )
    require(exit_code == 0, f"d/search vi exited with status {exit_code}")
    require(saved == "\ngamma\n",
            f"unexpected d/search buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"c", b"/", b"g", b"a", b"m", b"m", b"a", b"\r", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c/search vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c/search insert status")
    require(saved == "X\ngamma\n",
            f"unexpected c/search buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\ndelta\nbeta2\n",
        [b"/", b"b", b"e", b"t", b"a", b"\r", b"g", b"g", b"d", b"n"],
    )
    require(exit_code == 0, f"dn vi exited with status {exit_code}")
    require(saved == "beta\ngamma\ndelta\nbeta2\n",
            f"unexpected dn buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nmiddle\nword\n",
        [b"d", b"*"],
    )
    require(exit_code == 0, f"d* vi exited with status {exit_code}")
    require(saved == "word\n",
            f"unexpected d* buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nmiddle\nword\n",
        [b"c", b"*", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c* vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c* insert status")
    require(saved == "X\nword\n",
            f"unexpected c* buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nmiddle\nword\n",
        [b"G", b"d", b"#"],
    )
    require(exit_code == 0, f"d# vi exited with status {exit_code}")
    require(saved == "word\n",
            f"unexpected d# buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nmiddle\nword\n",
        [b"G", b"c", b"#", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c# vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c# insert status")
    require(saved == "X\nword\n",
            f"unexpected c# buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\nmatch1\nx\nmatch2\ny\nmatch3\n",
        [b"/", b"m", b"a", b"t", b"c", b"h", b"\r", b"g", b"g", b"d", b"2", b"n"],
    )
    require(exit_code == 0, f"d2n vi exited with status {exit_code}")
    require(saved == "match2\ny\nmatch3\n",
            f"unexpected d2n buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nx\nword\ny\nword\n",
        [b"g", b"g", b"d", b"2", b"*"],
    )
    require(exit_code == 0, f"d2* vi exited with status {exit_code}")
    require(saved == "word\n",
            f"unexpected d2* buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nx\nword\ny\nword\n",
        [b"G", b"d", b"2", b"#"],
    )
    require(exit_code == 0, f"d2# vi exited with status {exit_code}")
    require(saved == "word\n",
            f"unexpected d2# buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"2", b"$", b"r", b"X"],
    )
    require(exit_code == 0, f"2$ vi exited with status {exit_code}")
    require(saved == "one\ntwX\nthree\n",
            f"unexpected 2$ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"d", b"2", b"$"],
    )
    require(exit_code == 0, f"d2$ vi exited with status {exit_code}")
    require(saved == "three\n",
            f"unexpected d2$ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"c", b"2", b"$", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c2$ vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c2$ insert status")
    require(saved == "X\nthree\n",
            f"unexpected c2$ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"y", b"2", b"$", b"P"],
    )
    require(exit_code == 0, f"y2$ vi exited with status {exit_code}")
    require(saved == "one\ntwoone\ntwo\nthree\n",
            f"unexpected y2$ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n",
        [b"d", b"5", b"0", b"%"],
    )
    require(exit_code == 0, f"d50% vi exited with status {exit_code}")
    require(saved == "6\n7\n8\n9\n10\n",
            f"unexpected d50% buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n",
        [b"c", b"5", b"0", b"%", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c50% vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c50% insert status")
    require(saved == "X\n6\n7\n8\n9\n10\n",
            f"unexpected c50% buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n",
        [b"y", b"5", b"0", b"%", b"P"],
    )
    require(exit_code == 0, f"y50% vi exited with status {exit_code}")
    require(saved == "1\n2\n3\n4\n5\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n",
            f"unexpected y50% buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\n",
        [b"W", b"W", b"W", b"2", b"g", b"e", b"r", b"Y"],
    )
    require(exit_code == 0, f"ge-motion vi exited with status {exit_code}")
    require(saved == "one twY three four\n",
            f"unexpected ge-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"W", b"g", b"E", b"r", b"Z"],
    )
    require(exit_code == 0, f"gE-motion vi exited with status {exit_code}")
    require(saved == "one,twZ   three\n",
            f"unexpected gE-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\n",
        [b"W", b"W", b"W", b"d", b"g", b"e"],
    )
    require(exit_code == 0, f"dge vi exited with status {exit_code}")
    require(saved == "one two threour\n",
            f"unexpected dge buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\n",
        [b"W", b"W", b"W", b"c", b"g", b"e", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cge vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cge insert status")
    require(saved == "one two threXour\n",
            f"unexpected cge buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"W", b"d", b"g", b"E"],
    )
    require(exit_code == 0, f"dgE vi exited with status {exit_code}")
    require(saved == "one,twhree\n",
            f"unexpected dgE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"W", b"c", b"g", b"E", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cgE vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cgE insert status")
    require(saved == "one,twXhree\n",
            f"unexpected cgE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\n",
        [b"W", b"W", b"W", b"y", b"g", b"e", b"P"],
    )
    require(exit_code == 0, f"yge vi exited with status {exit_code}")
    require(saved == "one two three fe four\n",
            f"unexpected yge buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"W", b"y", b"g", b"E", b"P"],
    )
    require(exit_code == 0, f"ygE vi exited with status {exit_code}")
    require(saved == "one,two   to   three\n",
            f"unexpected ygE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\n  two\nthree\n\nfour\n",
        [b"}", b"}", b"{"],
    )
    require(exit_code == 0, f"paragraph-motion vi exited with status {exit_code}")
    require(decoded.count("line 3/6") >= 2, "missing paragraph backward/forward status")
    require("line 6/6" in decoded, "missing paragraph forward-to-end status")
    require(saved == "one\n\n  two\nthree\n\nfour\n",
            f"unexpected paragraph-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "Alpha one.  Beta two!\nGamma three?  Delta four.\n",
        [b")", b"r", b"1", b")", b"r", b"2"],
    )
    require(exit_code == 0, f"sentence-forward vi exited with status {exit_code}")
    require("line 1/2" in decoded, "missing sentence-forward line 1 status")
    require("line 2/2" in decoded, "missing sentence-forward line 2 status")
    require(saved == "Alpha one.  1eta two!\n2amma three?  Delta four.\n",
            f"unexpected sentence-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "Alpha one.  Beta two!\nGamma three?  Delta four.\n",
        [b"G", b"$", b"(", b"r", b"3", b"(", b"r", b"4"],
    )
    require(exit_code == 0, f"sentence-backward vi exited with status {exit_code}")
    require(decoded.count("line 2/2") >= 1, "missing sentence-backward line 2 status")
    require(decoded.count("line 1/2") >= 1, "missing sentence-backward line 1 status")
    require(saved == "Alpha one.  Beta two!\n4amma three?  3elta four.\n",
            f"unexpected sentence-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"\x1b[F", b"r", b"Z", b"\x1b[H", b"r", b"Y"],
    )
    require(exit_code == 0, f"home-end-motion vi exited with status {exit_code}")
    require(saved == "YbcdZ\n",
            f"unexpected home-end-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 41)),
        [b"1", b"0", b"G", b"\x04", b"\x15"],
    )
    require(exit_code == 0, f"scroll vi exited with status {exit_code}")
    require("line 10/40" in decoded, "missing initial half-page anchor")
    require("line 21/40" in decoded, "missing half-page down status")
    require(saved.startswith("line 1\nline 2\nline 3\n"),
            "unexpected half-page scroll buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 41)),
        [b"\x1b[6~", b"r", b"X", b"\x1b[5~", b"r", b"Y"],
    )
    require(exit_code == 0, f"page-key-scroll vi exited with status {exit_code}")
    require("line 23/40" in decoded, "missing page-down key status")
    require("line 1/40" in decoded, "missing page-up key status")
    require(saved.startswith("Yine 1\nline 2\nline 3\n"),
            "unexpected page-key-scroll buffer contents")
    require("Xine 23\n" in saved, "missing page-down key edit target")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 41)),
        [b"2", b"0", b"G", b"z", b"z", b"M", b"z", b"\r", b"H", b"z", b"-", b"L"],
    )
    require(exit_code == 0, f"z-position vi exited with status {exit_code}")
    require(decoded.count("line 20/40") >= 4, "missing z-position status transitions")
    require(saved.startswith("line 1\nline 2\nline 3\n"),
            "unexpected z-position buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nmatch one\nbb\nmatch two\ncc\nmatch three\n",
        [b"/", b"match\r", b"2", b"n", b"2", b"N"],
    )
    require(exit_code == 0, f"search-repeat vi exited with status {exit_code}")
    require("line 2/6" in decoded, "missing initial search status")
    require("line 6/6" in decoded, "missing counted n status")
    require(saved == "aa\nmatch one\nbb\nmatch two\ncc\nmatch three\n",
            "unexpected counted search buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\nalpha two\nbeta again\nalpha three\n",
        [b"*", b"n", b"#"],
    )
    require(exit_code == 0, f"word-search vi exited with status {exit_code}")
    require(decoded.count("line 3/5") >= 1, "missing star-search status")
    require(decoded.count("line 5/5") >= 1, "missing star repeat status")
    require(decoded.count("line 3/5") >= 2, "missing hash-search status")
    require(saved == "alpha\nbeta\nalpha two\nbeta again\nalpha three\n",
            "unexpected word-search buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (alpha[beta{gamma}delta]omega)\nnext line\n",
        [b"f", b"(", b"%", b"r", b"1", b"0", b"f", b"]", b"%", b"r", b"2"],
    )
    require(exit_code == 0, f"match-motion vi exited with status {exit_code}")
    require("line 1/2" in decoded, "missing match-motion status")
    require(saved == "if (alpha2beta{gamma}delta]omega1\nnext line\n",
            f"unexpected match-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (alpha[beta{gamma}delta]omega)\n",
        [b"0", b"f", b"(", b"d", b"%"],
    )
    require(exit_code == 0, f"match-delete vi exited with status {exit_code}")
    require(saved == "if \n",
            f"unexpected match-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (alpha[beta{gamma}delta]omega)\n",
        [b"0", b"f", b"(", b"c", b"%", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"match-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing match-change insert status")
    require(saved == "if X\n",
            f"unexpected match-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (alpha[beta{gamma}delta]omega)\n",
        [b"0", b"f", b"(", b"y", b"%", b"P"],
    )
    require(exit_code == 0, f"match-yank vi exited with status {exit_code}")
    require(saved == "if (alpha[beta{gamma}delta]omega)(alpha[beta{gamma}delta]omega)\n",
            f"unexpected match-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 11)),
        [b"5", b"0", b"%"],
    )
    require(exit_code == 0, f"percent-goto vi exited with status {exit_code}")
    require("line 5/10" in decoded, "missing percent-goto status")
    require(saved.startswith("line 1\nline 2\nline 3\n"),
            "unexpected percent-goto buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  one\n\tTwo\nthree\n",
        [b"+", b"\r", b"-"],
    )
    require(exit_code == 0, f"line-motion vi exited with status {exit_code}")
    require("line 2/3" in decoded, "missing plus-motion status")
    require("line 3/3" in decoded, "missing enter-motion status")
    require(saved == "  one\n\tTwo\nthree\n",
            "unexpected line-motion buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\n",
        [b"w", b"w", b"d", b"b", b"u", b"w", b"c", b"b", b"D", b"O", b"N", b"E", b"\x1b"],
    )
    require(exit_code == 0, f"backward-operator vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing backward change insert status")
    require(saved == "alpha DONEgamma\n",
            f"unexpected backward-operator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b"$", b"X", b".", b"u", b"$", b"2", b"X"],
    )
    require(exit_code == 0, f"backward-char vi exited with status {exit_code}")
    require("line 1/1" in decoded, "missing backward-char status")
    require(saved == "d\n",
            f"unexpected backward-char buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  alpha beta\n",
        [b"$", b"d", b"^", b"u", b"$", b"c", b"0", b"D", b"O", b"N", b"E", b"\x1b"],
    )
    require(exit_code == 0, f"line-start-operator vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing line-start change insert status")
    require(saved == "DONEa\n",
            f"unexpected line-start-operator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "AbCd\n",
        [b"~", b".", b"2", b"~"],
    )
    require(exit_code == 0, f"toggle-case vi exited with status {exit_code}")
    require("line 1/1" in decoded, "missing toggle-case status")
    require(saved == "aBcD\n",
            f"unexpected toggle-case buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcaXca\n",
        [b"f", b"a", b";", b",", b"l", b"r", b"1", b"$", b"F", b"a", b"h", b"r", b"2"],
    )
    require(exit_code == 0, f"find-motion vi exited with status {exit_code}")
    require(saved == "ab2a1ca\n",
            f"unexpected find-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"t", b"X", b"l", b";", b",", b"r", b"2", b"$", b"T", b"X", b"r", b"1"],
    )
    require(exit_code == 0, f"till-motion vi exited with status {exit_code}")
    require(saved == "abcX2efXghiX1\n",
            f"unexpected till-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [
            b"0", b"d", b"f", b"X", b"u",
            b"0", b"d", b"t", b"X", b"u",
            b"$", b"c", b"F", b"X", b"D", b"O", b"N", b"E", b"\x1b", b"u",
            b"$", b"c", b"T", b"X", b"T", b"A", b"I", b"L", b"\x1b",
        ],
    )
    require(exit_code == 0, f"operator-find vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing operator-find insert status")
    require(saved == "abcXdefXghiXTAIL\n",
            f"unexpected operator-find buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"0", b"d", b"f", b"X", b"."],
    )
    require(exit_code == 0, f"repeat-df vi exited with status {exit_code}")
    require(saved == "ghiXj\n",
            f"unexpected repeat-df buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"0", b"d", b"t", b"X", b"."],
    )
    require(exit_code == 0, f"repeat-dt vi exited with status {exit_code}")
    require(saved == "XghiXj\n",
            f"unexpected repeat-dt buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"0", b"f", b"X", b"d", b";"],
    )
    require(exit_code == 0, f"operator-semicolon-delete vi exited with status {exit_code}")
    require(saved == "abcghiXj\n",
            f"unexpected operator-semicolon-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"0", b"f", b"X", b"c", b";", b"T", b"A", b"I", b"L", b"\x1b"],
    )
    require(exit_code == 0, f"operator-semicolon-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing operator-semicolon change insert status")
    require(saved == "abcTAILghiXj\n",
            f"unexpected operator-semicolon-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"0", b"f", b"X", b"y", b";", b"P"],
    )
    require(exit_code == 0, f"operator-semicolon-yank vi exited with status {exit_code}")
    require(saved == "abcXdefXXdefXghiXj\n",
            f"unexpected operator-semicolon-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n  two\nthree\n",
        [b"j", b"m", b"a", b"G", b"'", b"a", b"r", b"T", b"G", b"`", b"a", b"r", b">"],
    )
    require(exit_code == 0, f"visual-mark vi exited with status {exit_code}")
    require("line 2/3" in decoded, "missing mark jump status")
    require(saved == "one\n> two\nthree\n",
            f"unexpected visual-mark buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"G", b"m", b"a", b"g", b"g", b"d", b"'", b"a"],
    )
    require(exit_code == 0, f"visual-mark-delete vi exited with status {exit_code}")
    require(saved == "",
            f"unexpected visual-mark-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"g", b"g", b"m", b"a", b"G", b"d", b"'", b"a"],
    )
    require(exit_code == 0, f"visual-mark-delete-upward vi exited with status {exit_code}")
    require(saved == "",
            f"unexpected visual-mark-delete-upward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"G", b"m", b"a", b"g", b"g", b"c", b"'", b"a", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-mark-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing visual-mark change insert status")
    require(saved == "X\n",
            f"unexpected visual-mark-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"G", b"m", b"a", b"g", b"g", b"y", b"'", b"a", b"P"],
    )
    require(exit_code == 0, f"visual-mark-yank vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\nfour\none\ntwo\nthree\nfour\n",
            f"unexpected visual-mark-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"g", b"g", b"l", b"l", b"m", b"a", b"j", b"`", b"a", b"r", b"Z"],
    )
    require(exit_code == 0, f"visual-backtick-jump vi exited with status {exit_code}")
    require(saved == "abZcd\nef)gh\n",
            f"unexpected visual-backtick-jump buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcdef\n",
        [b"l", b"l", b"m", b"a", b"$", b"d", b"`", b"a"],
    )
    require(exit_code == 0, f"visual-backtick-delete vi exited with status {exit_code}")
    require(saved == "abf\n",
            f"unexpected visual-backtick-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"g", b"g", b"l", b"l", b"m", b"a", b"G", b"c", b"`", b"a", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-backtick-change-cross vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing visual-backtick-change insert status")
    require(saved == "abX\nef)gh\n",
            f"unexpected visual-backtick-change-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"g", b"g", b"l", b"l", b"m", b"a", b"G", b"y", b"`", b"a", b"P"],
    )
    require(exit_code == 0, f"visual-backtick-yank-cross vi exited with status {exit_code}")
    require(saved == "ab(cd(cd\nef)gh\n",
            f"unexpected visual-backtick-yank-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"0", b"f", b"(", b"d", b"%"],
    )
    require(exit_code == 0, f"visual-percent-delete-cross vi exited with status {exit_code}")
    require(saved == "abgh\n",
            f"unexpected visual-percent-delete-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"0", b"f", b"(", b"c", b"%", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-percent-change-cross vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing visual-percent-change insert status")
    require(saved == "abXgh\n",
            f"unexpected visual-percent-change-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"0", b"f", b"(", b"y", b"%", b"P"],
    )
    require(exit_code == 0, f"visual-percent-yank-cross vi exited with status {exit_code}")
    require(saved == "ab(cd\nef)(cd\nef)gh\n",
            f"unexpected visual-percent-yank-cross buffer: {saved!r}")
    print("vi pty test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
