include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = personality.c perso_ipc_sem.c perso_ipc_shm.c perso_native.c perso_native64.c perso_freebsd.c perso_linux.c perso_svr3.c perso_svr4.c svr4/svr4_calls.c perso_sysv386.c perso_netbsd.c perso_openbsd.c perso_sunos.c perso_elks.c perso_xenix.c compat.c linux/linux_user.c linux/linux_sig.c freebsd/freebsd_sig.c freebsd/freebsd_user.c freebsd/freebsd_uname.c netbsd/netbsd_sig.c netbsd/netbsd_user.c openbsd/openbsd_sig.c sunos/sunos_user.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)perso.o

$(O)perso.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f perso.o $(OBJS) $(OBJS:.o=.d)
