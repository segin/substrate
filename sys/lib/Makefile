include ../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../arch/i386 -I.. -I../include
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = crc32.c string.c printf.c div64.c
OBJS = $(SRCS:.c=.o)

all: lib.o

lib.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
