include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../arch/$(ARCH) -I../../arch/x86-common
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = audio.c oss.c null_audio.c ac97.c sb16.c hda.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)audio_drv.o

$(O)audio_drv.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o *.d
