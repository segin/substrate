include ../../Makefile.inc

PROG = ld.so
SRCS = ld.so.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean install

all: $(PROG)

$(PROG): $(OBJS)
	$(LD) $(FREESTANDING_LDFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(PROG) $(OBJS)

install:
	@echo "Installing $(PROG)..."
	cp $(PROG) $(DESTDIR)/sbin/$(PROG) 
