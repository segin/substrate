include ../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../include -I../../include -I..
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = process.c sched.c wait.c pgrp.c rusage.c
OBJS = $(SRCS:.c=.o)

all: pm.o

pm.o: $(OBJS)
	$(LD) -m elf_i386 -r -o $@ $(OBJS)

clean:
	rm -f $(OBJS) pm.o
