#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

"$CC_BIN" -std=c23 native_c23_attr_std.c -o /tmp/cc_native_c23_attr_std 2>/tmp/cc_native_c23_attr_std.log
/tmp/cc_native_c23_attr_std
grep -q "deprecated function used: old_api" /tmp/cc_native_c23_attr_std.log
grep -q "ignoring nodiscard return value from must_use" /tmp/cc_native_c23_attr_std.log

! "$CC_BIN" -std=c23 -Werror native_c23_attr_std.c -o /tmp/cc_native_c23_attr_std_werror 2>/tmp/cc_native_c23_attr_std_werror.log
grep -q "deprecated function used: old_api" /tmp/cc_native_c23_attr_std_werror.log

! "$CC_BIN" -std=c23 -c native_bad_c23_attr_fallthrough_outside.c -o /tmp/cc_native_bad_c23_attr_fallthrough_outside.o

"$CC_BIN" -std=c23 -E native_c23_attr_has.c -o /tmp/cc_native_c23_attr_has.i
grep -q "has_deprecated_attr = 1;" /tmp/cc_native_c23_attr_has.i
grep -q "has_nodiscard_attr = 1;" /tmp/cc_native_c23_attr_has.i
grep -q "has_reproducible_attr = 1;" /tmp/cc_native_c23_attr_has.i
grep -q "has_unsequenced_attr = 1;" /tmp/cc_native_c23_attr_has.i
