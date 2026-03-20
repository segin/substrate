# find(1) Architecture

## Overview
`bin/find/` implements a POSIX.1-2024 `find` with FreeBSD-default semantics, OpenBSD/NetBSD deltas, and GNU findutils extensions.

## Components
- **`find.h`**: Shared types (AST nodes, per-file state), global traversal variables, debug flags.
- **`find_main.c`**: Main entry and orchestration (options → paths → expression → traversal).
- **`find_parse.c`**: Recursive descent parser and optimizer (cost-based AND reordering).
- **`find_eval.c`**: Expression evaluator, output actions, and exec helpers.
- **`find_traverse.c`**: Directory traversal engine with loop detection and xdev support.

## Semantic Token Classes
1. **Startup options**: `-H`, `-L`, `-P`, `-E`, `-s`, `-x`, `-X`, `-D`, `-O`, `-regextype`, `-f`, `-files0-from`.
2. **Global modifiers**: `-depth`, `-xdev`, `-maxdepth`, `-mindepth`, `-follow`, `-daystart`, etc.
3. **Pure tests**: `-name`, `-type`, `-perm`, `-newer`, `-regex`, etc.
4. **Actions**: `-print`, `-exec`, `-delete`, `-quit`, etc.

## Dialect and Features
- Conflict policy: `docs/find/conflicts.md`.
- Feature matrix: `docs/find/spec-baseline.md`.
