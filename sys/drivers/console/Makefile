include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../include -I../../arch/$(ARCH)
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = ansi_handler.c console.c tty.c vt.c pty.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)console_subsys.o

$(O)console_subsys.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o *.d
