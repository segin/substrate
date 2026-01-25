include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = vga.c fb.c fb_console.c font.c bga.c hw_text.c
OBJS = $(SRCS:.c=.o)

all: video.o

video.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
