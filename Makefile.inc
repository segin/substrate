# Common definitions for TestUnix build system

# Default architecture
ARCH ?= i386

# Toolchain definitions
CC ?= gcc
CXX ?= g++
LD ?= ld
AR ?= ar
AS ?= as

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# Base flags for 32-bit x86
COMMON_FLAGS = -m32 -Wall -Wextra -Werror -g -fno-stack-protector -mno-sse -mno-sse2 -mno-mmx
LIBC_INCLUDE = $(TOP)/lib/c/include
SYS_INCLUDE = $(TOP)/sys/include
GCC_INCLUDE = $(shell $(CC) -m32 -print-file-name=include)
CFLAGS += $(COMMON_FLAGS) -std=c23 -nostdinc -I$(LIBC_INCLUDE) -I$(SYS_INCLUDE) -I$(GCC_INCLUDE)
CXXFLAGS += $(COMMON_FLAGS) -std=c++23 -nostdinc -I$(LIBC_INCLUDE) -I$(SYS_INCLUDE) -I$(GCC_INCLUDE)

# Freestanding flags (for kernel and standalone utils)
FREESTANDING_CFLAGS = $(CFLAGS) -ffreestanding -nostdlib -fno-builtin -fno-pie
FREESTANDING_LDFLAGS = -m elf_i386 -nostdlib

# Userland flags (for standard libraries and utils)
# These will eventually link against our own libc
USER_CFLAGS = $(CFLAGS)
USER_LDFLAGS = -m32 -nostdlib -fno-pie
LIBGCC = $(shell $(CC) -m32 -print-libgcc-file-name)