include ../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../../sys
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = vm_page.c vm_map.c
OBJS = $(SRCS:.c=.o)

all: vm.o

vm.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o
