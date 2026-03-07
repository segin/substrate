# Makefile for usr.bin/compress
# Builds: compress, uncompress, zcat (symlink to compress)

PROGS=  compress uncompress
CFLAGS= -Wall -Wextra -Wno-unused-parameter -I.
BINDIR= /usr/bin

all: $(PROGS) zcat

compress: compress.o
	$(CC) $(CFLAGS) -o $@ compress.o

uncompress: uncompress.o
	$(CC) $(CFLAGS) -o $@ uncompress.o

zcat: compress
	ln -sf compress zcat

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f compress uncompress zcat *.o

install: $(PROGS) zcat
	mkdir -p $(DESTDIR)$(BINDIR)
	cp compress $(DESTDIR)$(BINDIR)/compress
	cp uncompress $(DESTDIR)$(BINDIR)/uncompress
	ln -sf compress $(DESTDIR)$(BINDIR)/zcat

.PHONY: all clean install
