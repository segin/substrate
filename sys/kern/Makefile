include ../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = acct.c time.c main.c panic.c stacktrace.c ksyms.c spinlock.c mutex.c semaphore.c kthread.c signal.c sigprop.c futex.c lib.c stubs.c input.c kobject.c console.c cmdline.c debug.c tty.c sleepq.c random.c \
       geom/geom_subr.c geom/geom_mbr.c geom/geom_gpt.c geom/geom_bsd.c
OBJS = $(SRCS:.c=.o)

all: kern.o

kern.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
