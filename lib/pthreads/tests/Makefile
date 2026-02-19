CC = gcc
CFLAGS = -Wall -g -DHOST_TEST -I.. -I../../../sys/include -I../../../sys -I../../../include

all: host_test_join

host_test_join: host_test_join.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f host_test_join
