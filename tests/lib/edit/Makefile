HOST_TESTS = host_test_history

all: $(HOST_TESTS)

host_test_history: host_test_history.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys -I../../include -I../../../lib/edit host_test_history.c -o $@

clean:
	rm -f $(HOST_TESTS)
