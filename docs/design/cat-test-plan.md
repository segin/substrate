# cat Test Plan (Linux Host First)

## Scope
The `cat` test suite validates:
- CLI flag semantics and combinations.
- Raw-mode read/write robustness.
- Cooked visualization correctness.
- Locking and error handling policies.
- Property-level invariants for byte visualization.
- Fuzz safety for cooked transformations.

## Layout
- `tests/bin/cat/test_cat_unit.py`: unit-level behavior checks.
- `tests/bin/cat/test_cat_integration.py`: pipeline/device/stdin/stdout integration checks.
- `tests/bin/cat/test_cat_property.py`: deterministic randomized property tests.
- `tests/bin/cat/test_cat_regression.py`: EINTR/short-io/malloc-fallback/lock regressions.
- `tests/bin/cat/fuzz/fuzz_cat_cooked.c`: coverage-guided fuzz target.
- `tests/bin/cat/fuzz/corpus/*`: initial fuzz seeds.

## Standard Execution
```sh
make -C tests/bin/cat test
```

## Sanitizer Execution
```sh
make -C tests/bin/cat san
```

## Fuzz Smoke
```sh
make -C tests/bin/cat fuzz-smoke
```

## CI Integration
Primary script:
```sh
tests/ci/test-cat.sh
```

Script flow:
1. Build host `cat` and hook-enabled test binary.
2. Run unit/integration/property/regression suites.
3. Run ASAN/UBSAN suite when `clang` is available.
4. Run libFuzzer smoke (`-runs=500`) when `clang` is available.

## Determinism
- Randomized tests use fixed PRNG seeds.
- Regression tests use deterministic hook injection rather than timing-dependent faults.
- Real lock wait behavior is measured with controlled lock hold intervals.

## Expected Exit
- Test command exits `0` only when all mandatory suites pass.
- Any semantic mismatch or robustness regression fails CI immediately.
