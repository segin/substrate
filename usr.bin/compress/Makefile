# Makefile for usr.bin/compress

PROG=   uncompress
SRCS=   uncompress.c
OBJS=   $(SRCS:.c=.o)
CFLAGS= -Wall -Wextra -Wno-unused-parameter -I.
BINDIR= /usr/bin

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(PROG) $(OBJS)

install: $(PROG)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)

.PHONY: all clean install
