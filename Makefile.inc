# Common definitions for Substrate build system

# Default architecture
ARCH ?= i386

# Toolchain definitions
CC ?= cc
LD ?= ld
AR ?= ar
AS ?= as
HOSTCC_DEFAULT := $(shell if [ -x /usr/bin/cc ]; then printf '%s' /usr/bin/cc; elif command -v cc >/dev/null 2>&1; then command -v cc; else printf '%s' cc; fi)
HOSTCC ?= $(HOSTCC_DEFAULT)

# Warning policy
WARNFLAGS ?= -Wall -Wextra
WERROR ?= -Werror
ifneq ($(NO_WERROR),1)
WARNFLAGS += $(WERROR)
endif

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# Header dependency tracking: every compile emits a <obj>.d via -MMD -MP (in
# COMMON_FLAGS); pull those fragments back in so editing a header rebuilds every
# object that includes it.  Without this an incremental `make` leaves objects
# compiled against a stale struct layout -- e.g. adding a field to
# usb_endpoint_t silently broke the (unrebuilt) USB HID drivers.  This must come
# AFTER TOP is computed: the .d files land in MAKEFILE_LIST, and TOP uses
# $(lastword $(MAKEFILE_LIST)).  Covers objects up to one subdir deep (fs/fat/).
-include $(wildcard *.d */*.d)
# The .d fragments define targets (foo.o: ...); since Makefile.inc is included
# first, one of them would otherwise become the default goal.  Clear it so the
# including Makefile's own first target (usually `all`) is the default again.
.DEFAULT_GOAL :=

# Branding utility for native Substrate binaries
BRANDELF = $(TOP)/usr.bin/brandelf/brandelf

# Default install destination
DESTDIR ?= $(TOP)/dist

# Base flags for 32-bit x86.
# -march=i486 is the substrate-wide ISA baseline: the kernel has
# always required CMPXCHG (introduced on i486), and the userland
# libpthread futex mutex needs LOCK CMPXCHG / LOCK XADD to inline
# the GCC __sync_* / __atomic_* builtins.  Without it those expand
# to libcalls (__sync_val_compare_and_swap_4 etc) that the
# substrate libgcc.a does not provide.  Putting it in COMMON_FLAGS
# rather than just KERNEL_CFLAGS keeps user binaries consistent.
COMMON_FLAGS = -m32 $(WARNFLAGS) -g -fno-stack-protector -mno-sse -mno-sse2 -mno-mmx -march=i486 -mtune=i486 -MMD -MP
USR_INCLUDE = $(TOP)/include
SYS_INCLUDE = $(TOP)/sys/include
GCC_INCLUDE = $(shell $(CC) -m32 -print-file-name=include 2>/dev/null)
CFLAGS += $(COMMON_FLAGS) -std=c2x -nostdinc -I$(SYS_INCLUDE) -I$(USR_INCLUDE) -I$(TOP)/sys
ifneq ($(strip $(GCC_INCLUDE)),)
ifneq ($(wildcard $(GCC_INCLUDE)),)
CFLAGS += -I$(GCC_INCLUDE)
endif
endif

# Freestanding flags (for kernel and standalone utils)
FREESTANDING_CFLAGS = $(CFLAGS) -ffreestanding -nostdlib -fno-builtin -fno-pie
FREESTANDING_LDFLAGS = -m elf_i386 -nostdlib

# Kernel source tree for internal includes (enables #include <kern/sched.h>)
KERNEL_SRC = $(TOP)/sys
KERNEL_I386_BASELINE_FLAGS = -march=i486 -mtune=i486
KERNEL_CFLAGS = $(FREESTANDING_CFLAGS) $(KERNEL_I386_BASELINE_FLAGS) -I$(KERNEL_SRC)

# Userland flags (for standard libraries and utils)
# These will eventually link against our own libc
# Userland flags (for standard libraries and utils)
# These will eventually link against our own libc
EXTRA_USER_CFLAGS := $(USER_CFLAGS)
EXTRA_USER_LDFLAGS := $(USER_LDFLAGS)

ifdef NATIVE_BUILD
    # Host build configuration
    HOSTCFLAGS ?= -O2 $(WARNFLAGS)
    CC := $(HOSTCC)
    CFLAGS := $(HOSTCFLAGS)
    # Start with empty/standard flags for host, but allow overrides.
    USER_CFLAGS := $(CFLAGS) $(EXTRA_USER_CFLAGS)
    USER_LDFLAGS := $(EXTRA_USER_LDFLAGS)
    LIBGCC :=
else
    # Target build configuration (Substrate i386).
    # -march=i486 already comes via COMMON_FLAGS.
    USER_CFLAGS := $(COMMON_FLAGS) -std=c2x -nostdinc -fno-pie -I$(USR_INCLUDE) -I$(SYS_INCLUDE) -I$(TOP)/sys
ifneq ($(strip $(GCC_INCLUDE)),)
ifneq ($(wildcard $(GCC_INCLUDE)),)
    USER_CFLAGS += -I$(GCC_INCLUDE)
endif
endif
    USER_CFLAGS += $(EXTRA_USER_CFLAGS)
    USER_LDFLAGS := -m32 -nostdlib -fno-pie -no-pie $(EXTRA_USER_LDFLAGS)
    _LIBGCC_PATH := $(shell $(CC) -m32 -print-libgcc-file-name 2>/dev/null)
    LIBGCC = $(if $(wildcard $(_LIBGCC_PATH)),$(_LIBGCC_PATH),)

    # Shared-library compile / link flags.  Same defines as
    # USER_CFLAGS but PIC instead of -fno-pie, and the link line
    # produces a versioned ET_DYN with a SONAME.  Library Makefiles
    # use these to produce libX.so.<N> alongside their libX.a.
    SHLIB_CFLAGS  := $(filter-out -fno-pie,$(USER_CFLAGS)) -fPIC
    # --no-as-needed: every library Makefile here writes its -l: flags
    # ahead of the objects ("$(CC) $(SHLIB_LDFLAGS) -l:libc.so.0 -o $@
    # $(PIC_OBJS)"), so on a distribution whose ld defaults to
    # --as-needed -- Debian and Ubuntu do; Arch does not -- ld sees the
    # libraries before any reference to them and records no DT_NEEDED at
    # all.  The result is a libc.so.0 with an empty needed list, and the
    # first thing the cross compiler tries to link dies on
    #
    #   libc.so.0: undefined reference to `feraiseexcept'
    #   libc.so.0: undefined reference to `syscall'
    #
    # because there is no longer a DT_NEEDED chain for
    # --copy-dt-needed-entries to follow.  These dependencies are
    # deliberate -- ld.so resolves them at run time -- so --as-needed is
    # simply the wrong policy for substrate's shared libraries.
    SHLIB_LDFLAGS := -m32 -nostdlib -nostartfiles -shared \
                     -Wl,--no-as-needed \
                     -Wl,-Bsymbolic-functions \
                     -Wl,-z,max-page-size=0x1000 \
                     -Wl,-z,now \
                     $(EXTRA_USER_LDFLAGS)
endif

# Macro to brand a binary as Substrate native (OSABI=64)
# Usage: $(call BRAND_SUBSTRATE,binary_name)
define BRAND_SUBSTRATE
	@set -e; \
	if [ -x $(BRANDELF) ]; then \
		if $(BRANDELF) -t Substrate $(1); then \
			:; \
		else \
			byte=$$(od -An -t u1 -j 7 -N 1 $(1) 2>/dev/null | tr -d '[:space:]'); \
			if [ "$$byte" != "64" ]; then \
				printf '\100' | dd of=$(1) bs=1 seek=7 conv=notrunc status=none; \
			fi; \
		fi; \
	else \
		printf '\100' | dd of=$(1) bs=1 seek=7 conv=notrunc status=none; \
	fi
endef
