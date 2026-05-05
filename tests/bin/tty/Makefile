TOP = ../../..
PYTHON ?= python3

TTY_BIN = $(abspath $(TOP)/bin/tty/tty)

.PHONY: all build test clean

all: test

build:
	$(MAKE) -C $(TOP)/bin/tty NATIVE_BUILD=1 clean
	$(MAKE) -C $(TOP)/bin/tty NATIVE_BUILD=1 all

test: build
	$(PYTHON) ./test_tty.py $(TTY_BIN)

clean:
