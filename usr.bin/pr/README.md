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
make -C usr.bin/pr test
```

This builds a host binary and runs `usr.bin/pr/tests/test_pr.sh`.
