# Substrate `wc` Implementation Notes

This document highlights the specific conflict-resolution constraints the Substrate `wc` project is built around to blend GNU coreutils features with POSIX.1-2024 compliance and a BSD-first fallback strategy.

## Key Feature Nuances

1. **`-c` / `-m` Exclusivity**: GNU allows providing both `-c` and `-m` flags, and its behavior will merge or duplicate the requested column output. BSD (FreeBSD, OpenBSD, NetBSD) treats these two byte/char length options as mutually exclusive. In this `wc` tool, supplying both flags simply lets the lastly-evaluated option override the first, silencing the other.
2. **Longest Line Definition (`-L`)**: In GNU `wc`, `-L` computes the *display width* using tab stops and `wcwidth()`. In BSD, `-L` strictly computes the raw sequence length (bytes by default, wide-characters if `-m` is active). The BSD logic is implemented here. Note that totals across files yield `MAX(file...)` not `SUM(file...)` under this property.
3. **Trailing Newlines**: Per POSIX and BSD, lines are incremented purely by `\n` characters regardless if EOF follows a trailing word without a newline character.
4. **Totals Formatting**: If the user provides `--total=only`, `wc` suppresses file paths entirely and operates purely on an aggregate sum output natively. 

## GNU Flags Supported
- `--files0-from=F`: Reads `\0` NUL delimited pipelines standardly used via `find -print0 | wc --files0-from=-`. No positional file parameters are allowed while this flag is processing.
- `--total={auto,always,only,never}`: Complete override capability of the standard totals reporting row. 
- `--debug`: Dumps extra signals explicitly to `stderr`.

## BSD Flags Supported
- `-h`: Uses unit scaling via base 2 standard (`K`, `M`, `G`) for sizes exceeding standard readable values.
- `--libxo`: Included in parsing signature.
- `SIGINFO` reporting via `stderr` for large streaming jobs on standard systems that pass it.
