HOST_TESTS = host_test_history test_editline

all: $(HOST_TESTS)

host_test_history: host_test_history.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys -I../../include -I../../../lib/edit host_test_history.c -o $@

test_editline: test_editline.c ../../../lib/edit/editline.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys -I../../include -I../../../lib/edit ../../../lib/edit/editline.c test_editline.c -o $@

clean:
	rm -f $(HOST_TESTS)
