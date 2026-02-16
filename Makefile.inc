# Common definitions for Substrate build system

# Default architecture
ARCH ?= i386

# Toolchain definitions
CC ?= gcc
CXX ?= g++
LD ?= ld
AR ?= ar
AS ?= as

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# Branding utility for native Substrate binaries
BRANDELF = $(TOP)/usr.bin/brandelf/brandelf

# Default install destination
DESTDIR ?= $(TOP)/dist

# Base flags for 32-bit x86
COMMON_FLAGS = -m32 -Wall -Wextra -g -fno-stack-protector -mno-sse -mno-sse2 -mno-mmx
USR_INCLUDE = $(TOP)/include
SYS_INCLUDE = $(TOP)/sys/include
GCC_INCLUDE = $(shell $(CC) -m32 -print-file-name=include)
CFLAGS += $(COMMON_FLAGS) -std=c2x -nostdinc -I$(SYS_INCLUDE) -I$(USR_INCLUDE) -I$(TOP)/sys -I$(GCC_INCLUDE)
CXXFLAGS += $(COMMON_FLAGS) -std=c++2b -nostdinc -I$(SYS_INCLUDE) -I$(USR_INCLUDE) -I$(TOP)/sys -I$(GCC_INCLUDE)

# Freestanding flags (for kernel and standalone utils)
FREESTANDING_CFLAGS = $(CFLAGS) -ffreestanding -nostdlib -fno-builtin -fno-pie
FREESTANDING_LDFLAGS = -m elf_i386 -nostdlib

# Kernel source tree for internal includes (enables #include <kern/sched.h>)
KERNEL_SRC = $(TOP)/sys
KERNEL_CFLAGS = $(FREESTANDING_CFLAGS) -I$(KERNEL_SRC)

# Userland flags (for standard libraries and utils)
# These will eventually link against our own libc
# Userland flags (for standard libraries and utils)
# These will eventually link against our own libc
ifdef NATIVE_BUILD
    # Host build configuration
    HOSTCC ?= cc
    HOSTCFLAGS ?= -O2 -Wall
    CC := $(HOSTCC)
    CFLAGS := $(HOSTCFLAGS)
    # Start with empty/standard LDFLAGS for host
    USER_LDFLAGS :=
    # Ensure no 32-bit override if host is 64-bit (unless HOSTCFLAGS specifies it)
else
    # Target build configuration (Substrate i386)
    USER_CFLAGS = $(CFLAGS) -I$(TOP)/sys
    USER_LDFLAGS = -m32 -nostdlib -fno-pie -no-pie
endif

LIBGCC = $(shell $(CC) -m32 -print-libgcc-file-name)

# Macro to brand a binary as Substrate native (OSABI=64)
# Usage: $(call BRAND_SUBSTRATE,binary_name)
define BRAND_SUBSTRATE
	@if [ -x $(BRANDELF) ]; then $(BRANDELF) -t Substrate $(1); fi
endef