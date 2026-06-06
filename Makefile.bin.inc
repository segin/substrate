# Common Makefile for simple single-file Substrate utilities
# Include this after setting PROG variable
#
# Usage in bin/foo/Makefile:
#   PROG = foo
#   include ../../Makefile.bin.inc

include ../../Makefile.inc

SRCS ?= $(PROG).c
# Host (NATIVE_BUILD=1) and target builds must not share .o files: the
# host produces ELFCLASS64 x86-64 objects, the target produces ELFCLASS32
# i386.  If a test runs `make NATIVE_BUILD=1` and a later target build
# re-enters the same directory, the stale host .o looks up-to-date vs.
# its .c source and the target link fails with "wrong format".  Suffix
# host objects with .host.o so the two modes coexist.
ifdef NATIVE_BUILD
OBJS = $(SRCS:.c=.host.o)
else
OBJS = $(SRCS:.c=.o)
endif
# Cleaning sweeps both mode suffixes — running `make clean` once should
# leave the directory clean regardless of which mode was last used.
ALL_MODE_OBJS = $(SRCS:.c=.o) $(SRCS:.c=.host.o)
EXTRA_CLEAN ?=
BUILD_RULE_DEPS = Makefile $(TOP)/Makefile.inc $(TOP)/Makefile.bin.inc

LIBC_DIR = $(TOP)/lib/c

# DYNAMIC=1 in a per-program Makefile (set BEFORE including this
# file) builds a PIE binary that loads /sbin/ld.so as its
# interpreter and resolves libc / libm / etc. through the
# dynamic linker.  The default remains static linking against
# libc.a so bootstrap-essential binaries stay self-contained.
#
# NATIVE_BUILD=1 overrides DYNAMIC: when building the host-side test
# binary, /sbin/ld.so doesn't exist and the host's libc isn't at
# $(TOP)/lib/c.  Host builds always fall through to the plain
# USER_CFLAGS/USER_LDFLAGS path regardless of DYNAMIC.
ifdef NATIVE_BUILD
    # Host build — DYNAMIC is irrelevant; let host cc do its default.
    BIN_CFLAGS  = $(USER_CFLAGS)
    LDFLAGS    := $(USER_LDFLAGS)
else ifeq ($(DYNAMIC),1)
    # Clean PIE flags — do NOT inherit USER_LDFLAGS' -fno-pie /
    # -no-pie or we'd get conflicting modes on the link line.
    # Deferred (=) like the static branch so per-program Makefiles
    # that do `USER_CFLAGS += -I...` AFTER including this file still
    # take effect — bc/Makefile is the canonical example.
    BIN_CFLAGS  = $(filter-out -fno-pie,$(USER_CFLAGS)) -fPIE
    LDFLAGS    := -m32 -nostdlib -pie -fPIE \
                  -Wl,--dynamic-linker=/sbin/ld.so \
                  -Wl,--unresolved-symbols=ignore-in-shared-libs
else
    # Deferred (=) so per-program Makefiles that do
    # `USER_CFLAGS += -I...` AFTER including this file still work
    # — the variable is re-expanded each time the compile rule fires.
    BIN_CFLAGS  = $(USER_CFLAGS)
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
    # Substrate build: manual linking.
    #
    # libc.a's fpclassify TU references feraiseexcept (in libm), so
    # every static-link binary needs libm too — otherwise the
    # static linker fails with "undefined reference".  The dynamic
    # path doesn't have this issue because libc.so.0 declares
    # DT_NEEDED libm.so.0 and ld.so resolves it at runtime.
    CRT0 = $(LIBC_DIR)/crt0.o
    ifeq ($(DYNAMIC),1)
        # libsys.so.0 owns the raw syscall() dispatcher (libc no longer
        # duplicates it).  libc.so.0 already DT_NEEDEDs it so it is loaded
        # at runtime, but a binary that calls syscall() directly (e.g.
        # ldtctl) also needs it on the link line, or ld can't resolve the
        # object's reference (it won't follow libc's DT_NEEDED because
        # lib/sys isn't in the search path).  Mirror the static branch.
        LIBS = -L$(LIBC_DIR) -l:libc.so.0 -L$(TOP)/lib/sys -l:libsys.so.0 $(LIBGCC)
    else
        # libsys.a owns the raw syscall() dispatcher (libc no longer
        # duplicates it), so every static binary links it.  --start-group
        # resolves the libc<->libsys references (libc's wrappers call
        # syscall; a pulled libsys wrapper references libc's errno)
        # regardless of archive order.
        LIBS = -Wl,--start-group -L$(LIBC_DIR) -lc -L$(TOP)/lib/m -lm -L$(TOP)/lib/sys -l:libsys.a -Wl,--end-group $(LIBGCC)
    endif
    BRAND_CMD = $(call BRAND_SUBSTRATE,$@)
endif

$(OBJS) $(PROG): $(BUILD_RULE_DEPS)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(CRT0) $(LDADD) $(LIBS)
	$(BRAND_CMD)

%.o: %.c
	$(CC) $(BIN_CFLAGS) -c $< -o $@

%.host.o: %.c
	$(CC) $(BIN_CFLAGS) -c $< -o $@

clean:
	rm -f $(PROG) $(ALL_MODE_OBJS) $(EXTRA_CLEAN)

BINDIR ?= $(DESTDIR)/bin

install: $(PROG)
	install -D $(PROG) $(BINDIR)/$(PROG)
