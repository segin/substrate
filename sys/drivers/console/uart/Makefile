include ../../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../../include -I../../../arch/i386
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = uart.c
OBJS = $(SRCS:.c=.o)

all: uart_drv.o

uart_drv.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
