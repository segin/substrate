#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
INC_DIR="-I$ROOT/include"

"$CC_BIN" -std=c11 native_c11_static_assert.c -o /tmp/cc_native_c11_static_assert
/tmp/cc_native_c11_static_assert

"$CC_BIN" -std=c11 native_c11_generic.c -o /tmp/cc_native_c11_generic
/tmp/cc_native_c11_generic

"$CC_BIN" -std=c11 native_c11_align_thread_noreturn.c -o /tmp/cc_native_c11_align_thread_noreturn
/tmp/cc_native_c11_align_thread_noreturn

"$CC_BIN" -std=c11 $INC_DIR native_c11_atomic.c -o /tmp/cc_native_c11_atomic
/tmp/cc_native_c11_atomic

"$CC_BIN" -std=c11 native_c11_unicode_literals.c -o /tmp/cc_native_c11_unicode_literals
/tmp/cc_native_c11_unicode_literals

! "$CC_BIN" -std=c11 -c native_bad_c11_static_assert_fail.c -o /tmp/cc_native_bad_c11_static_assert_fail.o
! "$CC_BIN" -std=c11 -c native_bad_c11_thread_local_local.c -o /tmp/cc_native_bad_c11_thread_local_local.o

"$CC_BIN" -std=c17 native_c17_register.c -o /tmp/cc_native_c17_register
/tmp/cc_native_c17_register

"$CC_BIN" -std=c11 -E native_c11_pp_version.c -o /tmp/cc_native_c11_pp_version.i
grep -q "stdc_version = 201112L;" /tmp/cc_native_c11_pp_version.i
grep -q "stdc_no_threads = 1;" /tmp/cc_native_c11_pp_version.i

"$CC_BIN" -std=c17 -E native_c11_pp_version.c -o /tmp/cc_native_c17_pp_version.i
grep -q "stdc_version = 201710L;" /tmp/cc_native_c17_pp_version.i
grep -q "stdc_no_threads = 1;" /tmp/cc_native_c17_pp_version.i
