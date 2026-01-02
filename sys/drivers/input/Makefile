include ../../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../../video -I../../../arch/i386
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = keyboard.c ps2.c
OBJS = $(SRCS:.c=.o)

all: input.o

input.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
