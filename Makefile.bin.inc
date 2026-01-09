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

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBC_DIR)/crt0.o -L$(LIBC_DIR) -lc $(LIBGCC)
	$(call BRAND_SUBSTRATE,$@)

%.o: %.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

clean:
	rm -f $(PROG) $(OBJS)

install: $(PROG)
	install -D $(PROG) $(DESTDIR)/bin/$(PROG)
