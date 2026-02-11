include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../include -I../../arch/i386
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = ansi_handler.c console.c tty.c vt.c
OBJS = $(SRCS:.c=.o)

all: console_subsys.o

console_subsys.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
