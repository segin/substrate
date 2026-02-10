include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../arch/i386
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = pseudo.c ntsync.c zero.c
OBJS = $(SRCS:.c=.o)

all: devices.o

devices.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
