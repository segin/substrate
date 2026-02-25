#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

i=0
while [ $i -lt 16 ]; do
	in="/tmp/cc_pp_fuzz_$i.c"
	dd if=/dev/urandom of="$in" bs=128 count=1 >/dev/null 2>&1 || true
	"$CC_BIN" -E "$in" -o /tmp/cc_pp_fuzz_out.i >/dev/null 2>/dev/null || true
	i=$((i + 1))
done

cat > /tmp/cc_pp_fuzz_seed.c <<'EOF'
#define A(x, ...) x __VA_OPT__(+ (__VA_ARGS__))
#include "pp_s3_quote.h"
#if defined(QQ) && (1 || (1 / 0))
int x = A(1, 2);
#endif
EOF
"$CC_BIN" -E -I. /tmp/cc_pp_fuzz_seed.c -o /tmp/cc_pp_fuzz_seed.i
grep -q "int x = 1 + (2);" /tmp/cc_pp_fuzz_seed.i
