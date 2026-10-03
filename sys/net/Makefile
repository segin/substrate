include ../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = af_unix.c netdev.c af_packet.c \
       inet.c inet_csum.c arp.c icmp.c inet6.c udp.c af_inet.c loopback.c tcp.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)net.o

$(O)net.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o *.d
