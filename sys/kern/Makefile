include ../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = acct.c time.c main.c panic.c stacktrace.c ksyms.c spinlock.c mutex.c semaphore.c kthread.c turnstile.c \
       signal.c sigprop.c futex.c kobject.c cmdline.c debug.c sleepq.c random.c syscall.c device.c driver.c bus.c sysctl.c sysinfo.c sched_smp.c runqueue.c \
       geom/geom_subr.c geom/geom_mbr.c geom/geom_gpt.c geom/geom_bsd.c
OBJS = $(SRCS:.c=.o)

all: kern.o

kern.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
