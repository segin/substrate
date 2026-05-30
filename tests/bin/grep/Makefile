TOP = ../../..

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra
PYTHON ?= python3

GREP_BIN  = $(abspath $(TOP)/bin/grep/grep)
GREP_ASAN = $(abspath ./grep_asan)
GREP_SRCS = $(TOP)/bin/grep/grep.c $(TOP)/bin/grep/grep_opts.c \
            $(TOP)/bin/grep/grep_pattern.c
REGEX_A   = $(TOP)/usr.lib/regex/libregex.a
SHIM      = regexshim

.PHONY: all clean build asan test posix ext class regression asan-test

all: test

# Host grep (and its egrep/fgrep symlinks) via the program Makefile.
build:
	$(MAKE) -C $(TOP)/bin/grep NATIVE_BUILD=1

test: build posix ext class regression

posix: build
	$(PYTHON) ./test_grep_posix.py $(GREP_BIN)

ext: build
	$(PYTHON) ./test_grep_ext.py $(GREP_BIN)

class: build
	$(PYTHON) ./test_grep_class.py $(GREP_BIN)

regression: build
	$(PYTHON) ./test_grep_regression.py $(GREP_BIN)

# ASAN build uses a regex-only include shim so every SYSTEM header comes from
# the host libc the binary links against (mixing substrate's sys/include with
# host libc mismatches struct stat -- see bin/grep/Makefile).
$(GREP_ASAN): $(GREP_SRCS)
	$(MAKE) -C $(TOP)/usr.lib/regex NATIVE_BUILD=1
	mkdir -p $(SHIM)/regex
	ln -sf $(abspath $(TOP)/include/regex.h) $(SHIM)/regex.h
	ln -sf $(abspath $(TOP)/include/regex/flags.h) $(SHIM)/regex/flags.h
	$(CC) $(CFLAGS) -fsanitize=address -I$(SHIM) $(GREP_SRCS) $(REGEX_A) \
	      -o $@

asan: $(GREP_ASAN)
	ln -sf grep_asan egrep
	ln -sf grep_asan fgrep

# Leak checking stays on; both grep and the regex engine are leak-clean.
ASAN_ENV = ASAN_OPTIONS=detect_leaks=1

asan-test: asan
	$(ASAN_ENV) $(PYTHON) ./test_grep_posix.py $(GREP_ASAN)
	$(ASAN_ENV) $(PYTHON) ./test_grep_ext.py $(GREP_ASAN)
	$(ASAN_ENV) $(PYTHON) ./test_grep_class.py $(GREP_ASAN)
	$(ASAN_ENV) $(PYTHON) ./test_grep_regression.py $(GREP_ASAN)

clean:
	rm -rf $(GREP_ASAN) $(SHIM) egrep fgrep
