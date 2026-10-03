include ../../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../../include -I../../../arch/$(ARCH)
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = uart.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)uart_drv.o

$(O)uart_drv.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
