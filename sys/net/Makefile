include ../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = af_unix.c netdev.c af_packet.c \
       inet.c arp.c icmp.c inet6.c udp.c af_inet.c loopback.c tcp.c
OBJS = $(SRCS:.c=.o)

all: net.o

net.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o *.d
