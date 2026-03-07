ifdef NATIVE_BUILD
all:
	@echo "Skipping ldtctl for host build"
clean:
install:
else
PROG = ldtctl
SRCS = ldtctl.c
EXTRA_CLEAN = setldt
USER_CFLAGS += -I../../sys/include

include ../../Makefile.bin.inc

all: setldt

setldt: ldtctl
	ln -sf ldtctl setldt

install-setldt:
	install -d $(BINDIR)
	ln -sf ldtctl $(BINDIR)/setldt

install: install-setldt
endif
