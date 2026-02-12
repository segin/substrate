# basename

`basename` - return non-directory portion of a pathname

## Synopsis

`basename string [suffix]`

## Description

The `basename` utility deletes any prefix ending with the last slash `/` character present in `string` (after first stripping trailing slashes), and a `suffix`, if given. The `suffix` is not removed if it is identical to the remaining characters in `string`. The resulting filename is written to the standard output.

## Options

None.

## Exit Status

The `basename` utility exits 0 on success, and >0 if an error occurs.

## Examples

    $ basename /usr/bin/sort
    sort

    $ basename include/stdio.h .h
    stdio

    $ basename /usr/bin/
    bin

    $ basename /
    /
