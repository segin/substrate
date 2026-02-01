include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = vga.c fb.c fb_console.c font_8x16.c font_8x8.c bga.c hw_text.c bios_menu.c
OBJS = $(SRCS:.c=.o)

all: video.o

video.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
