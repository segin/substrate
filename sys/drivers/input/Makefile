include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../video -I$(SYS_INCLUDE)
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = keyboard.c ps2.c mouse.c
OBJS = $(SRCS:.c=.o)

all: input.o

input.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
