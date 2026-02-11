CC = gcc
CFLAGS = -Wall -Wextra -g

TESTS = test_string test_clock_logic test_time_c23

all: $(TESTS)
	@echo "Running tests..."
	@for t in $(TESTS); do \
		echo "=== $$t ==="; \
		./$$t; \
	done

test_string: test_string.c ../src/string.c
	$(CC) $(CFLAGS) $< -o $@

test_clock_logic: test_clock_logic.c
	$(CC) $(CFLAGS) $< -o $@

test_time_c23: test_time_c23.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TESTS)
