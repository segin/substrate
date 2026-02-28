# chmod Test Harness

This directory contains unit, integration, property, and fuzz tests for `bin/chmod`.

## Quick Start

Build host `chmod` binary:

```sh
make -C ../../bin/chmod clean
make -C ../../bin/chmod NATIVE_BUILD=1
```

Run full suite:

```sh
make clean
make all
make ci CHMOD_BIN=../../bin/chmod/chmod
```

## Targets

- `run-unit`: parser unit tests and CLI behavior unit tests.
- `run-integration`: recursive policy and error-handling integration tests.
- `run-property`: randomized tree safety checks.
- `run-fuzz`: seeded fuzz runs for parser and traversal path handling.
- `ci`: runs all categories.

## Sanitizers

Use sanitizer flags locally:

```sh
make clean
make all SAN_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
make ci CHMOD_BIN=../../bin/chmod/chmod
```

Set deterministic property test parameters:

- `CHMOD_PROP_SEED` (default: `1337`)
- `CHMOD_PROP_ITERS` (default: `60`)
