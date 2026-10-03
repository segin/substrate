include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../include -I../../arch/$(ARCH) -I../../arch/x86-common

SRCS = rtl8139.c e1000.c e1000e.c r8168.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)net_drv.o

$(O)net_drv.o: $(OBJS)
	$(LD) $(FREESTANDING_LDFLAGS) -r -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.d
