# cat Test Plan (Linux Host First)

## Scope
The suite validates:
- CLI option behavior (short + long forms).
- Raw-mode byte preservation and streaming behavior.
- Cooked transformations (`-n/-b/-s/-E/-T/-v/-A/-e/-t`).
- Error continuation and exit-code policy.
- Locking and signal interruption behavior.
- Broken-pipe (`EPIPE`) handling.

## Layout
- `tests/bin/cat/test_cat_unit.py`: option and behavior unit coverage.
- `tests/bin/cat/test_cat_integration.py`: multi-file, stdin, FIFO, socket-path, tty/pipe/file stdout integration.
- `tests/bin/cat/test_cat_property.py`: deterministic properties (raw byte preservation, line-count invariants, visualization).
- `tests/bin/cat/test_cat_regression.py`: deterministic short I/O, `EINTR`, lock retries/failures, malloc fallback, `EPIPE` regression.
- `tests/bin/cat/fuzz/fuzz_cat_cooked.c`: coverage-guided cooked-logic fuzz target.
- `tests/bin/cat/fuzz/corpus/*`: seed corpus.

## Commands
```sh
make -C tests/bin/cat test
make -C tests/bin/cat san
make -C tests/bin/cat fuzz-smoke
```

## CI Integration
Primary script:
```sh
tests/ci/test-cat.sh
```

Script flow:
1. Build host `cat` and hook-enabled helper binary.
2. Run unit/integration/property/regression suites.
3. Run ASAN/UBSAN suite when `clang` is available.
4. Run fuzz smoke when `clang` is available.
5. Run valgrind spot-check when `valgrind` is available.

## Determinism
- Randomized tests use fixed seeds.
- Regression tests rely on hook-driven deterministic fault injection.
- Fuzz smoke copies seed corpus to a temporary directory so repository corpus remains stable.

## Pass Criteria
- All mandatory suites pass.
- Sanitizer and valgrind checks are clean when available.
- CI exits non-zero on any behavior regression.
