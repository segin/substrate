# uncompress

`uncompress` is a utility to decompress LZW-compressed files (.Z).

## Usage

    uncompress [-cfv] [file ...]

- `-c`: Write to stdout.
- `-f`: Force overwrite.
- `-v`: Verbose.

## Build

    make

## Test

    sh tests/usr.bin/compress/test_uncompress.sh
