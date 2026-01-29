include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = perso_native.c perso_freebsd.c perso_linux.c perso_svr3.c perso_svr4.c perso_netbsd.c perso_openbsd.c perso_sunos.c compat.c linux/linux_user.c
OBJS = $(SRCS:.c=.o)

all: perso.o

perso.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
