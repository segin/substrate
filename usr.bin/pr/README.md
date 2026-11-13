# pr(1) utility

## Build

Target build:

```sh
make -C usr.bin/pr
```

Host/native build:

```sh
make -C usr.bin/pr NATIVE_BUILD=1
```

## Run tests

```sh
make -C tests/usr.bin/pr run
```

This builds a host binary and runs `tests/usr.bin/pr/test_pr.sh`.
`make -C usr.bin/pr test` does the same after cleaning the build.
