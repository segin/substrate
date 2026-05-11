include ../../Makefile.inc

PROG=   lex
SRCS=   main.c options.c parser.c symtab.c regex.c dfa.c codegen.c
OBJS=   $(SRCS:.c=.o)
CFLAGS_LOCAL = -I.
BINDIR= /usr/bin

# Dynamic-link the target build: PIE + /sbin/ld.so + libc.so.0.
# Mirrors the toggle in Makefile.bin.inc so this hand-rolled
# Makefile produces the same artifact shape as the simpler ones.
TARGET_CFLAGS = $(filter-out -fno-pie,$(USER_CFLAGS)) -fPIE
TARGET_LDFLAGS = -m32 -nostdlib -pie -fPIE \
                 -Wl,--dynamic-linker=/sbin/ld.so \
                 -Wl,--unresolved-symbols=ignore-in-shared-libs

all: $(PROG)

$(PROG): $(OBJS)
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) -o $@ $(OBJS)
else
	$(CC) $(TARGET_LDFLAGS) -o $@ $(OBJS) $(TOP)/lib/c/crt0.o \
	      -L$(TOP)/lib/c -l:libc.so.0 $(LIBGCC)
	$(call BRAND_SUBSTRATE,$@)
endif

%.o: %.c
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) $(CFLAGS_LOCAL) -idirafter ../../include -c $< -o $@
else
	$(CC) $(TARGET_CFLAGS) $(CFLAGS_LOCAL) -c $< -o $@
endif

clean:
	rm -f $(PROG) $(OBJS)

install: $(PROG)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)

.PHONY: all clean install
