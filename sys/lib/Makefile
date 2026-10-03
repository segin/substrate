include ../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../arch/$(ARCH) -I.. -I../include
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = crc32.c string.c printf.c div64.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)lib.o

$(O)lib.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o *.d
