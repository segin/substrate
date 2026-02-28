# chmod Fuzzing

## Targets

- `fuzz_mode_parser`: exercises mode parser with seeded and mutated symbolic/numeric expressions.
- `fuzz_traversal_paths`: exercises recursive traversal paths and symlink policy combinations on generated trees.

## Seed Corpus

- `corpus/mode/*`
- `corpus/path/*`

## Run

```sh
make all
./fuzz_mode_parser corpus/mode 20000
CHMOD_BIN=../../bin/chmod/chmod ./fuzz_traversal_paths corpus/path 300
```

## Reproducing a Crash

1. Rebuild with sanitizers:

```sh
make clean
make all SAN_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
```

2. Re-run the failing command with the same corpus seed and iteration count.
3. If needed, reduce input set to a single corpus file and replay:

```sh
./fuzz_mode_parser corpus/mode 1
```

or

```sh
CHMOD_BIN=../../bin/chmod/chmod ./fuzz_traversal_paths corpus/path 1
```

4. Capture stderr with stack trace and attach the exact command, sanitizer output, and seed file.
