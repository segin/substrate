include ../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../arch/i386 -I.. -I../include
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = acct.c time.c main.c panic.c spinlock.c mutex.c semaphore.c kthread.c signal.c futex.c lib.c stubs.c input.c kobject.c console.c cmdline.c \
       geom/geom_subr.c geom/geom_mbr.c geom/geom_bsd.c geom/geom_gpt.c
OBJS = $(SRCS:.c=.o)

all: kern.o

kern.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
