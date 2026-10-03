include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../arch/$(ARCH) -I../../../tests
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = pseudo.c full.c null.c ntsync.c kmem.c kmem_test.c zero.c mem.c mem_test_helper.c lpt.c cpuid.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)devices.o

$(O)devices.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o *.d
