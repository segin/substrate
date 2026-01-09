include ../../Makefile.inc

PROG = brandelf
SRCS = brandelf.c

# Build for host to ensure stability/compatibility with host libc when verifying
# We override the include paths to use system headers.
CC = gcc
CFLAGS = -m32 -Wall -Wextra -g

all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(PROG) $(PROG).o

install: $(PROG)
	@echo "Installing $(PROG)..."
	@mkdir -p $(DESTDIR)/usr/bin
	@cp $(PROG) $(DESTDIR)/usr/bin/$(PROG)
