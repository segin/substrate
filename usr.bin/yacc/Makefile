# Makefile for usr.bin/yacc

PROG=   yacc
SRCS=   main.c reader.c lr0.c lalr.c mkpar.c output.c graph.c symtab.c warshall.c closure.c verbose.c
OBJS=   $(SRCS:.c=.o)
CFLAGS= -Wall -Wextra -Wno-unused-parameter -I.
BINDIR= /usr/bin

# Test binaries
TEST_OBJS = test_yacc.o

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test: $(PROG) test_yacc.sh
	sh test_yacc.sh

clean:
	rm -f $(PROG) $(OBJS) $(TEST_OBJS) y.tab.c y.tab.h y.output *.o

install:
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)

.PHONY: all clean install test
