include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../arch/i386 -I../../kern
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = elf.c pe.c coff.c ../exec.c
OBJS = $(SRCS:.c=.o)

all: exec.o

exec.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
