include ../../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = vga.c
OBJS = $(SRCS:.c=.o)

all: video.o

video.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
