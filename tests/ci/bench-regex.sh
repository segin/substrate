#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

cd "$ROOT"

make -C usr.lib/regex
cc -O2 -Iinclude -o /tmp/bench_regex usr.lib/regex/bench/bench_regex.c usr.lib/regex/libregex.a
/tmp/bench_regex
