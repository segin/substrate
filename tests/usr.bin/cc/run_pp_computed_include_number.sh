#!/bin/sh
# A computed #include's header name is spelled from its tokens: the
# space expansion keeps between tokens that would otherwise lex as one
# is no part of it.  Linux's trace headers include
# `<trace/events/system.h>` with system the TRACE_SYSTEM, and for 9p cc
# looked for "trace/events/9p .h" (net/9p/client.c: "include file not
# found").  gcc's output is the judge, both what the includes bring in
# and the space it too keeps in text.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_pp_computed_include_number: skip (no gcc)"
	exit 0
fi

mkdir -p "$CC_TMP/inc/trace/events"
echo "int from_9p = 1;" >"$CC_TMP/inc/trace/events/9p.h"
echo "int from_sched = 2;" >"$CC_TMP/inc/trace/events/sched.h"
echo "int from_quoted = 3;" >"$CC_TMP/inc/trace/events/2x.h"
cat >"$CC_TMP/t.c" <<'EOF2'
#define TRACE_INCLUDE_FILE TRACE_SYSTEM
#define __TRACE_INCLUDE(system) <trace/events/system.h>
#define TRACE_INCLUDE(system) __TRACE_INCLUDE(system)
#define __stringify_1(x...) #x
#define __stringify(x...) __stringify_1(x)
#define TRACE_SYSTEM 9p
#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched
#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)
#define QUOTED(system) __stringify(trace/events/system.h)
#include QUOTED(2x)
#define SHOW(x) x
SHOW(TRACE_INCLUDE(9p))
EOF2
gcc -E -P -I"$CC_TMP/inc" "$CC_TMP/t.c" | tr -s ' \t\n' '   ' | sed 's/^ *//' >"$CC_TMP/g.i"
"$CC_BIN" -E -P -I"$CC_TMP/inc" "$CC_TMP/t.c" | tr -s ' \t\n' '   ' | sed 's/^ *//' >"$CC_TMP/c.i"
if ! cmp -s "$CC_TMP/g.i" "$CC_TMP/c.i"; then
	echo "run_pp_computed_include_number: preprocessed text differs from gcc's:"
	diff "$CC_TMP/g.i" "$CC_TMP/c.i" || true
	exit 1
fi
echo "run_pp_computed_include_number: ok"
