include ../../Makefile.inc

PROG = cpio
SRCS = cpio.c

CFLAGS += -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700

all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(PROG) *.o

install: $(PROG)
	@echo "Installing $(PROG)..."
	@mkdir -p $(DESTDIR)/usr/bin
	@cp $(PROG) $(DESTDIR)/usr/bin/$(PROG)
