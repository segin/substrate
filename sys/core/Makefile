include ../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../arch/i386 -I../drivers/video -I../vfs -I../fs
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = main.c panic.c
OBJS = $(SRCS:.c=.o)

all: core.o

core.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
