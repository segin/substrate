include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../include -I../../arch/i386 -I../../arch/x86-common

SRCS = rtl8139.c
OBJS = $(SRCS:.c=.o)

all: net_drv.o

net_drv.o: $(OBJS)
	$(LD) $(FREESTANDING_LDFLAGS) -r -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.d
