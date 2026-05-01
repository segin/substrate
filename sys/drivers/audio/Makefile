include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../arch/i386 -I../../arch/x86-common
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = audio.c null_audio.c
OBJS = $(SRCS:.c=.o)

all: audio_drv.o

audio_drv.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
