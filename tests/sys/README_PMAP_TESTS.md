# PMAP Tests

## Overview
Comprehensive test suite for pmap_create/destroy functionality.

## Test Files

### test_pmap.c
Unit tests covering:
- Lifecycle (create/destroy)
- Multiple pmaps coexistence
- Kernel pmap protection
- NULL pointer handling
- Memory leak detection

### property_pmap.c
Property-based tests verifying invariants:
- Creation/destruction is idempotent
- All pmaps are page-aligned
- All pmaps have unique addresses
- Kernel pmap is immutable

### gen_fuzz_pmap.py
Fuzzing test generator:
- Generates random create/destroy sequences
- Tests edge cases and crash resistance
- 1000 operations per run

## Running Tests

```bash
# Run all tests (when integrated into kernel)
make test-pmap

# Generate fuzzing tests
python3 tests/gen_fuzz_pmap.py
```

## Expected Results
- All unit tests should PASS
- All property tests should PASS
- Fuzzing should complete without crashes
- Memory usage should return to baseline
