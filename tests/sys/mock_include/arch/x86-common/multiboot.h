/*
 * Mock redirect for the acct_compress host test.
 *
 * acct.c -> <drivers/video/vga.h> -> the real sys/drivers/video/fb.h, which
 * does `#include <arch/x86-common/multiboot.h>`.  The kernel moved multiboot.h
 * from arch/x86-common/include/ to arch/x86-common/; point at the real header
 * so the include resolves under -Imock_include.
 */
#include "../../../../../sys/arch/x86-common/multiboot.h"
