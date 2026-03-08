include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = personality.c perso_native.c perso_freebsd.c perso_linux.c perso_svr3.c perso_svr4.c perso_netbsd.c perso_openbsd.c perso_sunos.c perso_elks.c compat.c linux/linux_user.c linux/linux_sig.c freebsd/freebsd_sig.c netbsd/netbsd_sig.c netbsd/netbsd_user.c openbsd/openbsd_sig.c sunos/sunos_user.c
OBJS = $(SRCS:.c=.o)

all: perso.o

perso.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
