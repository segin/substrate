# dirname

`dirname` - return the directory portion of a pathname

## Synopsis

`dirname [-z|--zero] string [string ...]`

## Description

The `dirname` utility strips the trailing `/` and the last component from
each `string`, writing the directory portion to standard output. A
`string` with no `/` yields `.`; a `string` of just `/` yields `/`. The
path computation is POSIX `dirname(3)`, on which BSD and GNU agree.

Comprehensive across POSIX/BSD/GNU: a single operand is POSIX/BSD
behaviour; multiple operands and `-z` (NUL separator) follow GNU. Where
BSD and GNU differ, BSD wins — option parsing stops at the first operand
(no GNU permutation).

## Options

- `-z`, `--zero` — separate output with NUL instead of newline.
- `--help`, `--version`.

## Exit Status

0 on success, >0 on error.

## Examples

    $ dirname /usr/bin/sort
    /usr/bin

    $ dirname stdio.h
    .

    $ dirname /
    /
