include ../../Makefile.inc

PROG = ar
SRCS = ar.c

# Include path for elf.h (target headers)
CFLAGS += -I$(TOP)/sys/exec/formats

# Links for ranlib
LINKS = $(DESTDIR)/usr/bin/ar $(DESTDIR)/usr/bin/ranlib

all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(PROG) $(PROG).o

install: $(PROG)
	@echo "Installing $(PROG)..."
	@mkdir -p $(DESTDIR)/usr/bin
	@cp $(PROG) $(DESTDIR)/usr/bin/$(PROG)
	@ln -sf $(PROG) $(DESTDIR)/usr/bin/ranlib
