include ../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../arch/i386 -I..
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = acct.c time.c main.c panic.c spinlock.c mutex.c semaphore.c kthread.c signal.c futex.c lib.c stubs.c input.c
OBJS = $(SRCS:.c=.o)

all: kern.o

kern.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
