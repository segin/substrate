# Makefile for usr.bin/yacc

include ../../Makefile.inc

PROG=   yacc
SRCS=   main.c reader.c lr0.c lalr.c mkpar.c output.c graph.c symtab.c warshall.c closure.c verbose.c
OBJS=   $(SRCS:.c=.o)
CFLAGS_LOCAL = -Wno-unused-parameter -I.
BINDIR= /usr/bin

# Test binaries
TEST_OBJS = test_yacc.o

all: $(PROG)

$(PROG): $(OBJS)
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) -o $@ $(OBJS)
else
	$(CC) $(USER_LDFLAGS) -o $@ $(OBJS) $(TOP)/lib/c/crt0.o -L$(TOP)/lib/c -lc $(LIBGCC)
	$(call BRAND_SUBSTRATE,$@)
endif

%.o: %.c
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) $(CFLAGS_LOCAL) -idirafter ../../include -c $< -o $@
else
	$(CC) $(USER_CFLAGS) $(CFLAGS_LOCAL) -c $< -o $@
endif

test: $(PROG) test_yacc.sh
	sh test_yacc.sh

clean:
	rm -f $(PROG) $(OBJS) $(TEST_OBJS) y.tab.c y.tab.h y.output *.o

install: $(PROG)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)

.PHONY: all clean install test
