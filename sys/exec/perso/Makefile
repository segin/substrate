include ../../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../../arch/i386
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = perso_native.c perso_freebsd.c perso_linux.c
OBJS = $(SRCS:.c=.o)

all: perso.o

perso.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
