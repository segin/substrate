# readlink

`readlink` - print the target of a symbolic link, optionally canonicalized

## Synopsis

`readlink [-femnqsvz] [--] file [file ...]`

## Description

By default `readlink` prints the contents of each symbolic-link `file`
(the value from `readlink(2)`); it is an error if `file` is not a symlink.
With a canonicalize option it resolves `file` to an absolute pathname,
following every symlink.

Comprehensive across POSIX/BSD/GNU. Where BSD and GNU differ, BSD wins:
option parsing stops at the first operand (no GNU permutation); `-f`
canonicalizes allowing the final component to be missing (FreeBSD
`realpath` semantics); `-n` suppresses the trailing separator entirely;
errors are silent unless `-v`.

## Options

- `-f`, `--canonicalize` — resolve all symlinks; all but the last component must exist.
- `-e`, `--canonicalize-existing` — like `-f`, every component must exist.
- `-m`, `--canonicalize-missing` — like `-f`, no component need exist.
- `-n`, `--no-newline` — no trailing separator.
- `-z`, `--zero` — NUL separator instead of newline.
- `-q`/`--quiet`, `-s`/`--silent` — suppress errors (default).
- `-v`, `--verbose` — report errors.
- `--help`, `--version`.

## Exit Status

0 if every operand resolved, >0 otherwise.

## Examples

    $ readlink /usr/bin/cc
    gcc

    $ readlink -f -- "$0"        # canonical path of the running script
    /opt/trinity/bin/starttde
