# Common Makefile for simple single-file Substrate utilities
# Include this after setting PROG variable
#
# Usage in bin/foo/Makefile:
#   PROG = foo
#   include ../../Makefile.bin.inc

include ../../Makefile.inc

SRCS ?= $(PROG).c
OBJS = $(SRCS:.c=.o)
EXTRA_CLEAN ?=
BUILD_RULE_DEPS = Makefile $(TOP)/Makefile.inc $(TOP)/Makefile.bin.inc

LIBC_DIR = $(TOP)/lib/c

# DYNAMIC=1 in a per-program Makefile (set BEFORE including this
# file) builds a PIE binary that loads /sbin/ld.so as its
# interpreter and resolves libc / libm / etc. through the
# dynamic linker.  The default remains static linking against
# libc.a so bootstrap-essential binaries stay self-contained.
ifeq ($(DYNAMIC),1)
    # Clean PIE flags — do NOT inherit USER_LDFLAGS' -fno-pie /
    # -no-pie or we'd get conflicting modes on the link line.
    BIN_CFLAGS := $(filter-out -fno-pie,$(USER_CFLAGS)) -fPIE
    LDFLAGS    := -m32 -nostdlib -pie -fPIE \
                  -Wl,--dynamic-linker=/sbin/ld.so \
                  -Wl,--unresolved-symbols=ignore-in-shared-libs
else
    BIN_CFLAGS := $(USER_CFLAGS)
    LDFLAGS    := $(USER_LDFLAGS)
endif

.PHONY: all clean install

ifneq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(filter all,$(MAKECMDGOALS)),)
all: clean
.NOTPARALLEL: clean all
endif
endif

all: $(PROG)

ifdef NATIVE_BUILD
    # Native build: use host toolchain defaults
    CRT0 =
    LIBS =
    BRAND_CMD =
else
    # Substrate build: manual linking
    CRT0 = $(LIBC_DIR)/crt0.o
    ifeq ($(DYNAMIC),1)
        # Dynamic: ld.so will resolve symbols at runtime.
        LIBS = -L$(LIBC_DIR) -l:libc.so.0 $(LIBGCC)
    else
        LIBS = -L$(LIBC_DIR) -lc $(LIBGCC)
    endif
    BRAND_CMD = $(call BRAND_SUBSTRATE,$@)
endif

$(OBJS) $(PROG): $(BUILD_RULE_DEPS)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(CRT0) $(LDADD) $(LIBS)
	$(BRAND_CMD)

%.o: %.c
	$(CC) $(BIN_CFLAGS) -c $< -o $@

clean:
	rm -f $(PROG) $(OBJS) $(EXTRA_CLEAN)

BINDIR ?= $(DESTDIR)/bin

install: $(PROG)
	install -D $(PROG) $(BINDIR)/$(PROG)
