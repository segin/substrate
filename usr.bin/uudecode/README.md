# uudecode

Decodes files produced by `uuencode`.

## Usage
`uudecode [-p] [-s] [-o output_file] [file ...]`

- `-p`: Write to stdout.
- `-s`: Strip path components from output filename (secure mode).
- `-o`: Specify output file.

## Features
- Standard uuencoding support.
- Safe default behavior (compliant with POSIX but offers secure mode).
- Integration with standard tools.
