include ../../Makefile.inc

CC = gcc
AR = ar
CFLAGS := -m32 -Wall -fno-builtin -nostdinc -I../../include -I../../sys/include -I../../sys
ASFLAGS := -m32

OBJS = syscall.o vm86.o proc.o sysctl.o sysinfo.o getpagesize.o mlock.o getpid.o getuid.o pgrp.o getrusage.o times.o ioctl.o stime.o ptrace.o reboot.o

all: libsys.a

libsys.a: $(OBJS)
	$(AR) rcs $@ $^

syscall.o: syscall.S
	$(CC) $(ASFLAGS) -c -o $@ $<

vm86.o: vm86.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o libsys.a
