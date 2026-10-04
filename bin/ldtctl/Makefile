ifdef NATIVE_BUILD
all:
	@echo "Skipping ldtctl for host build"
clean:
install:
else
PROG = ldtctl
SRCS = ldtctl.c
EXTRA_CLEAN = $(O)setldt
USER_CFLAGS += -I../../sys/include

DYNAMIC = 1

include ../../Makefile.bin.inc

# $(O): see Makefile.inc (i386 in place, x86_64 under obj-x86_64/).  The
# link sits beside the program it names, so it waits for that one and not
# for the other architecture's.
all: $(O)setldt

$(O)setldt: $(O)ldtctl
	ln -sf ldtctl $@

install-setldt:
	install -d $(BINDIR)
	ln -sf ldtctl $(BINDIR)/setldt

install: install-setldt
endif
