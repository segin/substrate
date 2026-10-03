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
# The HOST build of brandelf (see usr.bin/brandelf/Makefile): this is run
# during the build, so it must be a binary the build host can exec.  The
# target brandelf sitting beside it is the one that ships.
BRANDELF = $(TOP)/usr.bin/brandelf/brandelf.host

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
#
# ARCH selects the target of everything built through this file.  The
# userland libraries build for either (lib/Makefile): i386 objects in place
# and installed under /lib and /usr/lib, x86_64 objects under each
# directory's $(O) and installed under /lib64 and /usr/lib64, so the two
# sets sit side by side in one tree and one root.
ifeq ($(ARCH),x86_64)
ARCH_M := -m64
ARCH_CPU_FLAGS := -march=x86-64
LIBDIR := lib64
else
ARCH_M := -m32
ARCH_CPU_FLAGS := -mno-sse -mno-sse2 -mno-mmx -march=i486 -mtune=i486
LIBDIR := lib
endif
COMMON_FLAGS = $(ARCH_M) $(WARNFLAGS) -g -fno-stack-protector $(ARCH_CPU_FLAGS) -MMD -MP
USR_INCLUDE = $(TOP)/include
SYS_INCLUDE = $(TOP)/sys/include
GCC_INCLUDE = $(shell $(CC) $(ARCH_M) -print-file-name=include 2>/dev/null)
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

# The 64-bit kernel (make -C sys ARCH=x86_64; docs/specs/arch_x86_64_core.md)
# builds from the same subdirectory Makefiles, but into its own object tree:
# every object lands under sys/arch/x86_64/obj/<subdir>/, so an i386 and an
# x86_64 build never share a .o and either can follow the other without a
# clean.  Subdirectory Makefiles name their objects $(O)foo.o.  $(O) is empty
# for i386, which therefore builds in place exactly as before.
#
# Outside sys/ the same $(O) convention serves the userland libraries:
# their x86_64 objects, archives and shared objects go to obj-x86_64/ in
# each library's own directory, compiled with the user flags below.
ifeq ($(ARCH),x86_64)
ifeq ($(filter $(KERNEL_SRC)/%,$(CURDIR)/),)
O := obj-x86_64/
-include $(wildcard $(O)*.d $(O)*/*.d $(O)*/*/*.d $(O)*/*/*/*.d)
.DEFAULT_GOAL :=
else
O := $(KERNEL_SRC)/arch/x86_64/obj/$(patsubst $(KERNEL_SRC)/%,%,$(CURDIR))/
KERNEL_CFLAGS := -m64 -march=x86-64 -mcmodel=kernel -mno-red-zone \
	-mgeneral-regs-only -ffreestanding -fno-builtin -fno-stack-protector \
	-fno-pie -fno-pic -fno-asynchronous-unwind-tables \
	-fno-omit-frame-pointer -ffunction-sections -fdata-sections \
	-std=c2x $(WARNFLAGS) -g -MMD -MP -nostdinc \
	-I$(SYS_INCLUDE) -I$(USR_INCLUDE) -I$(KERNEL_SRC) \
	-isystem $(shell $(CC) -print-file-name=include) \
	-DSUBSTRATE_ARCH_X86_64
FREESTANDING_CFLAGS := $(KERNEL_CFLAGS)
FREESTANDING_LDFLAGS := -m elf_x86_64 -nostdlib
CFLAGS := $(KERNEL_CFLAGS)

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -D__ASSEMBLER__ -c $< -o $@

-include $(wildcard $(O)*.d $(O)*/*.d)
.DEFAULT_GOAL :=
endif
else
O :=
endif

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
    # Target build configuration (Substrate, $(ARCH)).
    # The ISA baseline already comes via COMMON_FLAGS.
    USER_CFLAGS := $(COMMON_FLAGS) -std=c2x -nostdinc -fno-pie -I$(USR_INCLUDE) -I$(SYS_INCLUDE) -I$(TOP)/sys
ifneq ($(strip $(GCC_INCLUDE)),)
ifneq ($(wildcard $(GCC_INCLUDE)),)
    USER_CFLAGS += -I$(GCC_INCLUDE)
endif
endif
    USER_CFLAGS += $(EXTRA_USER_CFLAGS)
    USER_LDFLAGS := $(ARCH_M) -nostdlib -fno-pie -no-pie $(EXTRA_USER_LDFLAGS)
    _LIBGCC_PATH := $(shell $(CC) $(ARCH_M) -print-libgcc-file-name 2>/dev/null)
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
    SHLIB_LDFLAGS := $(ARCH_M) -nostdlib -nostartfiles -shared \
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
