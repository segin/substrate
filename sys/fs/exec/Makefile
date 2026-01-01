include ../../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../../arch/i386 -I../../kern
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = elf.c pe.c
OBJS = $(SRCS:.c=.o)

all: exec.o

exec.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
