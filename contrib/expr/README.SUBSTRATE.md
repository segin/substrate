# expr — Substrate port of OpenBSD bin/expr

OpenBSD's `bin/expr` (518 LOC, BSD-2 / UCB) is the upstream — small,
self-contained, pure POSIX expression evaluator.  The shape mirrors
`bin/tr` (the OpenBSD tr port we already have): in-tree `expr.c`
and `expr.1` plus a substrate patch series in `patches/`.

## Build chain
- `fetch.sh` — no-op for OpenBSD single-file ports; the source is
  already in-tree.  Just applies the substrate patch series in
  `series` to a working copy under `build/`.
- `build.sh` — compiles the patched source with the stage-1 cross
  toolchain and stages the binary + man page into
  `${SUBSTRATE_TOP}/dist-overlay/dist-expr/usr/`.

## Substrate adaptations
0001-drop-pledge.patch — strip the OpenBSD `pledge("stdio")` call;
substrate has no equivalent sandboxing primitive.
