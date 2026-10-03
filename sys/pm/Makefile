include ../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I.
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = process.c sched.c wait.c pgrp.c rusage.c
OBJS = $(addprefix $(O),$(SRCS:.c=.o))

all: $(O)pm.o

$(O)pm.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f $(OBJS) pm.o
