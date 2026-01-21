include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../arch/i386
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = uart.c
OBJS = $(SRCS:.c=.o)

all: serial.o

serial.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
