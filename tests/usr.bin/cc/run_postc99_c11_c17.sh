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

"$CC_BIN" -std=c11 native_c11_atomic_wrapper_type.c -o /tmp/cc_native_c11_atomic_wrapper_type
/tmp/cc_native_c11_atomic_wrapper_type

"$CC_BIN" -std=c11 native_c11_atomic_fence.c -o /tmp/cc_native_c11_atomic_fence
/tmp/cc_native_c11_atomic_fence
"$CC_BIN" -std=c11 -S native_c11_atomic_fence.c -o /tmp/cc_native_c11_atomic_fence.s
grep -q "lock; orl \$0, (%rsp)" /tmp/cc_native_c11_atomic_fence.s
grep -q "# asm clobber memory" /tmp/cc_native_c11_atomic_fence.s
"$CC_BIN" -std=c11 -m32 -S native_c11_atomic_fence.c -o /tmp/cc_native_c11_atomic_fence_32.s
grep -q "lock; orl \$0, (%esp)" /tmp/cc_native_c11_atomic_fence_32.s
grep -q "# asm clobber memory" /tmp/cc_native_c11_atomic_fence_32.s

"$CC_BIN" -std=c11 native_c11_unicode_literals.c -o /tmp/cc_native_c11_unicode_literals
/tmp/cc_native_c11_unicode_literals

! "$CC_BIN" -std=c11 -c native_bad_c11_static_assert_fail.c -o /tmp/cc_native_bad_c11_static_assert_fail.o
! "$CC_BIN" -std=c11 -c native_bad_c11_thread_local_local.c -o /tmp/cc_native_bad_c11_thread_local_local.o
! "$CC_BIN" -std=c11 -c native_bad_c11_atomic_memorder_nonconst.c -o /tmp/cc_native_bad_c11_atomic_memorder_nonconst.o
! "$CC_BIN" -std=c11 -c native_bad_c11_atomic_memorder_load_release.c -o /tmp/cc_native_bad_c11_atomic_memorder_load_release.o
! "$CC_BIN" -std=c11 -c native_bad_c11_atomic_memorder_store_acquire.c -o /tmp/cc_native_bad_c11_atomic_memorder_store_acquire.o
! "$CC_BIN" -std=c11 -c native_bad_c11_atomic_memorder_range.c -o /tmp/cc_native_bad_c11_atomic_memorder_range.o

"$CC_BIN" -std=c17 native_c17_register.c -o /tmp/cc_native_c17_register
/tmp/cc_native_c17_register
"$CC_BIN" -std=c17 -Wall -Wextra -Werror native_c17_empty_param_no_oldstyle.c -o /tmp/cc_native_c17_empty_param_no_oldstyle
/tmp/cc_native_c17_empty_param_no_oldstyle
! "$CC_BIN" -std=c17 -Wall -Werror -c native_bad_c17_register_obsolescent.c -o /tmp/cc_native_bad_c17_register_obsolescent.o
! "$CC_BIN" -std=c17 -Wall -Werror -c native_bad_c17_oldstyle_obsolescent.c -o /tmp/cc_native_bad_c17_oldstyle_obsolescent.o

"$CC_BIN" -std=c11 native_c11_pp_optional_macros.c -o /tmp/cc_native_c11_pp_optional_macros
/tmp/cc_native_c11_pp_optional_macros

"$CC_BIN" -std=c99 native_c99_pp_no_optional_macros.c -o /tmp/cc_native_c99_pp_no_optional_macros
/tmp/cc_native_c99_pp_no_optional_macros

"$CC_BIN" -std=c11 -E native_c11_pp_version.c -o /tmp/cc_native_c11_pp_version.i
grep -q "stdc_version = 201112L;" /tmp/cc_native_c11_pp_version.i
grep -q "stdc_no_threads = 1;" /tmp/cc_native_c11_pp_version.i
grep -q "stdc_no_complex = 1;" /tmp/cc_native_c11_pp_version.i

"$CC_BIN" -std=c17 -E native_c11_pp_version.c -o /tmp/cc_native_c17_pp_version.i
grep -q "stdc_version = 201710L;" /tmp/cc_native_c17_pp_version.i
grep -q "stdc_no_threads = 1;" /tmp/cc_native_c17_pp_version.i
grep -q "stdc_no_complex = 1;" /tmp/cc_native_c17_pp_version.i
