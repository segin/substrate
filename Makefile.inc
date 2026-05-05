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

# Branding utility for native Substrate binaries
BRANDELF = $(TOP)/usr.bin/brandelf/brandelf

# Default install destination
DESTDIR ?= $(TOP)/dist

# Base flags for 32-bit x86
COMMON_FLAGS = -m32 $(WARNFLAGS) -g -fno-stack-protector -mno-sse -mno-sse2 -mno-mmx
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
    # Target build configuration (Substrate i386)
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
