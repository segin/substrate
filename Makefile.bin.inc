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

LDFLAGS := $(USER_LDFLAGS)
LIBC_DIR = $(TOP)/lib/c

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
    LIBS = -L$(LIBC_DIR) -lc $(LIBGCC)
    BRAND_CMD = $(call BRAND_SUBSTRATE,$@)
endif

$(OBJS) $(PROG): $(BUILD_RULE_DEPS)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(CRT0) $(LDADD) $(LIBS)
	$(BRAND_CMD)

%.o: %.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

clean:
	rm -f $(PROG) $(OBJS) $(EXTRA_CLEAN)

BINDIR ?= $(DESTDIR)/bin

install: $(PROG)
	install -D $(PROG) $(BINDIR)/$(PROG)
