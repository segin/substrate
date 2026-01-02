include ../../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../../video -I../../../arch/i386 -I../../../include
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = keyboard.c ps2.c mouse.c
OBJS = $(SRCS:.c=.o)

all: input.o

input.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
