# Host-built unit tests for substrate's libpthread internals.
# Moved here from lib/pthread/tests/ — the convention is that all
# tests live under tests/ (matches every other library: tests/lib/c,
# tests/lib/m, tests/lib/edit, ...).
#
# These tests directly #include lib/pthread/pthread.c with HOST_TEST
# defined so the internal trampoline / mutex / join paths can be
# unit-tested against the host's libc.
TOP := ../../../..
CC = gcc
# -m32 is load-bearing: substrate's <sys/syscall.h> redirects to
# arch/i386/syscall.h on __i386__ and arch/x86_64/syscall.h
# otherwise; only the i386 header defines SYS_FUTEX (which
# pthread_mutex.c references at top level).
CFLAGS = -m32 -Wall -g -DHOST_TEST \
         -I$(TOP)/lib/pthread \
         -I$(TOP)/sys/include \
         -I$(TOP)/sys \
         -I$(TOP)/include

all: host_test_join host_test_mutex host_test_trampoline

host_test_join: host_test_join.c
	$(CC) $(CFLAGS) $< -o $@

host_test_mutex: host_test_mutex.c
	$(CC) $(CFLAGS) $< -o $@

host_test_trampoline: host_test_trampoline.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f host_test_join host_test_mutex host_test_trampoline
