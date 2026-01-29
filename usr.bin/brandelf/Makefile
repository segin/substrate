include ../../Makefile.inc

PROG = brandelf
SRCS = brandelf.c



all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(PROG) $(PROG).o

install: $(PROG)
	@echo "Installing $(PROG)..."
	@mkdir -p $(DESTDIR)/usr/bin
	@cp $(PROG) $(DESTDIR)/usr/bin/$(PROG)
