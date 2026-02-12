include ../../Makefile.inc

PROG = pax
SRCS = ar_io.c ar_subs.c buf_subs.c cache.c cpio.c file_subs.c ftree.c \
       gen_subs.c getoldopt.c options.c pat_rep.c pax.c sel_subs.c tables.c \
       tar.c tty_subs.c

LIBC = $(TOP)/lib/c/libc.a
CRT0 = $(TOP)/lib/c/crt0.o

all: $(PROG)

$(PROG): $(SRCS) $(LIBC) $(CRT0)
	$(CC) $(CFLAGS) -nostdlib -o $@ $(CRT0) $(SRCS) $(LIBC) $(LIBGCC)

clean:
	rm -f $(PROG) *.o

install: $(PROG)
	@echo "Installing $(PROG)..."
	@mkdir -p $(DESTDIR)/usr/bin
	@cp $(PROG) $(DESTDIR)/usr/bin/$(PROG)
