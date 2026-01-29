# Common Makefile for simple single-file Substrate utilities
# Include this after setting PROG variable
#
# Usage in bin/foo/Makefile:
#   PROG = foo
#   include ../../Makefile.bin.inc

include ../../Makefile.inc

SRCS ?= $(PROG).c
OBJS = $(SRCS:.c=.o)

LDFLAGS := $(USER_LDFLAGS)
LIBC_DIR = $(TOP)/lib/c

.PHONY: all clean install

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

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(CRT0) $(LDADD) $(LIBS)
	$(BRAND_CMD)

%.o: %.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

clean:
	rm -f $(PROG) $(OBJS)

BINDIR ?= $(DESTDIR)/bin

install: $(PROG)
	install -D $(PROG) $(BINDIR)/$(PROG)
