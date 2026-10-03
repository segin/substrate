include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../video -I$(SYS_INCLUDE)
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = input_subsys.c keyboard.c ps2.c mouse.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)input.o

$(O)input.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o *.d
