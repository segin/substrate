include ../../Makefile.inc

PROG=   slex
SRCS=   main.c options.c parser.c symtab.c rules.c regex.c dfa.c codegen.c \
        util.c
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

# The fixed text of the generated scanner, as C string arrays.
skel.h: scanner.skel mkskel.awk
	awk -f mkskel.awk scanner.skel > $@.tmp
	mv $@.tmp $@

codegen.o: skel.h

%.o: %.c
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) $(CFLAGS_LOCAL) -idirafter ../../include -c $< -o $@
else
	$(CC) $(TARGET_CFLAGS) $(CFLAGS_LOCAL) -c $< -o $@
endif

# Behaviour tests: build for the host and run tests/usr.bin/slex.
test:
	$(MAKE) clean
	$(MAKE) NATIVE_BUILD=1
	sh ../../tests/usr.bin/slex/run_tests.sh ./$(PROG)

clean:
	rm -f $(PROG) $(OBJS) skel.h skel.h.tmp

install: $(PROG)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)

.PHONY: all clean install test
