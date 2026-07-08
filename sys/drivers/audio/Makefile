include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../arch/i386 -I../../arch/x86-common
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = audio.c oss.c null_audio.c ac97.c sb16.c hda.c
OBJS = $(SRCS:.c=.o)

all: audio_drv.o

audio_drv.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o *.d
