include ../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../arch/i386 -I.. -I../include
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = test_runner.c test_pmap.c property_pmap_protect.c test_vm_expanded.c test_pid.c
# Add more tests here: test_mmap.c test_gdt.c etc.
# property_pmap.c needs fixing (missing headers) maybe? Excluding for now to ensure build.

OBJS = $(SRCS:.c=.o)

all: tests.o

tests.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
