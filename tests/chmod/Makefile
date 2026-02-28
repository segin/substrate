CC ?= cc
PYTHON ?= python3

CHMOD_BIN ?= ../../bin/chmod/chmod
CFLAGS_COMMON ?= -O2 -g -Wall -Wextra -Werror -std=c11
SAN_FLAGS ?=

CFLAGS = $(CFLAGS_COMMON) $(SAN_FLAGS)
LDFLAGS = $(SAN_FLAGS)

.PHONY: all clean chmod-bin test run-unit run-integration run-property run-fuzz ci

all: unit_mode_parser unit_cli fuzz_mode_parser fuzz_traversal_paths

chmod-bin:
	$(MAKE) -C ../../bin/chmod clean
	$(MAKE) -C ../../bin/chmod NATIVE_BUILD=1

unit_mode_parser: unit_mode_parser.c ../../bin/chmod/mode_parser.c ../../bin/chmod/mode_parser.h
	$(CC) $(CFLAGS) -I../../bin/chmod $< ../../bin/chmod/mode_parser.c -o $@ $(LDFLAGS)

unit_cli: unit_cli.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

fuzz_mode_parser: fuzz_mode_parser.c ../../bin/chmod/mode_parser.c ../../bin/chmod/mode_parser.h
	$(CC) $(CFLAGS) -I../../bin/chmod $< ../../bin/chmod/mode_parser.c -o $@ $(LDFLAGS)

fuzz_traversal_paths: fuzz_traversal_paths.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run-unit: unit_mode_parser unit_cli
	./unit_mode_parser
	CHMOD_BIN=$(CHMOD_BIN) ./unit_cli

run-integration:
	CHMOD_BIN=$(CHMOD_BIN) $(PYTHON) integration_chmod.py

run-property:
	CHMOD_BIN=$(CHMOD_BIN) $(PYTHON) property_chmod.py

run-fuzz: fuzz_mode_parser fuzz_traversal_paths
	./fuzz_mode_parser corpus/mode 8000
	CHMOD_BIN=$(CHMOD_BIN) ./fuzz_traversal_paths corpus/path 120

test: all run-unit run-integration run-property

ci: all run-unit run-integration run-property run-fuzz

clean:
	rm -f unit_mode_parser unit_cli fuzz_mode_parser fuzz_traversal_paths
