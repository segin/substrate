include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../arch/$(ARCH) -I../../kern
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = elf.c pe.c coff.c aout.c elks_aout.c xout.c xout286.c script.c ../exec.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)exec.o

$(O)exec.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

# Track the -MMD header deps so a proc.h (thread_t layout) change rebuilds
# every object here -- crucially ../exec.o, which lives one dir up and was
# NOT covered by the old `rm -f *.o` clean, so it went stale when a struct
# field was inserted mid-thread_t and its writes then landed on the wrong
# fields (a nasty layout-mismatch corruption).
-include $(OBJS:.o=.d)

clean:
	rm -f *.o *.d ../exec.o ../exec.d
