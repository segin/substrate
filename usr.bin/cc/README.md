# usr.bin/cc

This directory contains the textual SSA IR tooling for the compiler pipeline.

## Tools

- `ir-verifier`: parse and verify SSA IR invariants.
- `ir-normalize`: parse, verify, and emit canonicalized textual IR.
- `ir-diff`: normalize+compare two IR files.

## Current verifier checks

- module has a name and target.
- each function has at least one block.
- each block is non-empty and ends in a terminator.
- terminators only appear as last instruction in each block.
- CFG successors reference existing blocks.
- phi nodes are grouped at block start.
- phi incoming count matches predecessor count.
- SSA definitions are unique per function.
- no use of undefined SSA values.
- non-phi uses are dominated by their definition.
- intra-block use-before-def is rejected.
- all blocks are reachable from function entry.

## Build

```sh
make -C usr.bin/cc NATIVE_BUILD=1
```

## Test

```sh
make -C tests/usr.bin/cc test
```
