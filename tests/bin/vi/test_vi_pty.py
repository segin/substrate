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


def run_vi_session(vi_path, initial_text, key_steps, final_timeout=0.3, rows=24, cols=80):
    fd, temp_path = tempfile.mkstemp(prefix="exvi-", text=True)
    os.write(fd, initial_text.encode("utf-8"))
    os.close(fd)

    pid, master_fd = pty.fork()
    if pid == 0:
        os.execv(vi_path, [vi_path, temp_path])

    fcntl.ioctl(master_fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    output = read_some(master_fd, 0.4)
    for step in key_steps:
        output = send_keys(master_fd, output, step)
    output = send_keys(master_fd, output, b":")
    output = send_keys(master_fd, output, b"wq\r", final_timeout)

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
    require("defghijklmnopqrstuvZ" in decoded, "missing tab-aware far-right long-line content")
    require(saved == "a\tbcdefghijklmnopqrstuvZxyz\n",
            f"unexpected tab-column-motion buffer: {saved!r}")

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
    require(saved == "a\n",
            f"unexpected backward-char buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  alpha beta\n",
        [b"$", b"d", b"^", b"u", b"$", b"c", b"0", b"D", b"O", b"N", b"E", b"\x1b"],
    )
    require(exit_code == 0, f"line-start-operator vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing line-start change insert status")
    require(saved == "DONE\n",
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
    require(saved == "abca12a\n",
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
        "one\n  two\nthree\n",
        [b"j", b"m", b"a", b"G", b"'", b"a", b"r", b"T", b"G", b"`", b"a", b"r", b">"],
    )
    require(exit_code == 0, f"visual-mark vi exited with status {exit_code}")
    require("line 2/3" in decoded, "missing mark jump status")
    require(saved == "one\n> two\nthree\n",
            f"unexpected visual-mark buffer: {saved!r}")
    print("vi pty test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
