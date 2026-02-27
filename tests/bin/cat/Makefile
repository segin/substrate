TOP = ../../..

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Werror
PYTHON ?= python3

CAT_BIN = $(abspath $(TOP)/bin/cat/cat)
CAT_HOOK_BIN = $(abspath ./cat_test_hooks)
CAT_ASAN_BIN = $(abspath ./cat_asan)
CAT_ASAN_HOOK_BIN = $(abspath ./cat_asan_hooks)
FUZZ_BIN = $(abspath ./fuzz_cat_cooked)

CAT_SRCS = $(TOP)/bin/cat/cat.c $(TOP)/bin/cat/cat_cooked.c
CAT_INCLUDES = -I$(TOP)/bin/cat

.PHONY: all clean build build-hooks test unit integration property regression san fuzz-build fuzz-smoke

all: test

build:
	$(MAKE) -C $(TOP)/bin/cat NATIVE_BUILD=1

build-hooks: $(CAT_HOOK_BIN)

$(CAT_HOOK_BIN): $(CAT_SRCS)
	$(CC) $(CFLAGS) -DCAT_TEST_HOOKS $(CAT_INCLUDES) -o $@ $(CAT_SRCS)

test: build build-hooks unit integration property regression

unit:
	$(PYTHON) ./test_cat_unit.py $(CAT_BIN)

integration:
	$(PYTHON) ./test_cat_integration.py $(CAT_BIN)

property:
	$(PYTHON) ./test_cat_property.py $(CAT_BIN)

regression:
	$(PYTHON) ./test_cat_regression.py $(CAT_BIN) $(CAT_HOOK_BIN)

san: $(CAT_ASAN_BIN) $(CAT_ASAN_HOOK_BIN)
	$(PYTHON) ./test_cat_unit.py $(CAT_ASAN_BIN)
	$(PYTHON) ./test_cat_integration.py $(CAT_ASAN_BIN)
	$(PYTHON) ./test_cat_property.py $(CAT_ASAN_BIN)
	$(PYTHON) ./test_cat_regression.py $(CAT_ASAN_BIN) $(CAT_ASAN_HOOK_BIN)

$(CAT_ASAN_BIN): $(CAT_SRCS)
	$(CC) -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(CAT_INCLUDES) -o $@ $(CAT_SRCS)

$(CAT_ASAN_HOOK_BIN): $(CAT_SRCS)
	$(CC) -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
		-DCAT_TEST_HOOKS $(CAT_INCLUDES) -o $@ $(CAT_SRCS)

fuzz-build:
	@if command -v clang >/dev/null 2>&1; then \
		clang -O1 -g -Wall -Wextra -fsanitize=fuzzer,address,undefined \
			$(CAT_INCLUDES) \
			-o $(FUZZ_BIN) ./fuzz/fuzz_cat_cooked.c $(TOP)/bin/cat/cat_cooked.c; \
	else \
		echo "clang not available; skipping fuzz-build"; \
	fi

fuzz-smoke: fuzz-build
	@if [ -x "$(FUZZ_BIN)" ]; then \
		tmpdir=$$(mktemp -d); \
		cp ./fuzz/corpus/* $$tmpdir/; \
		$(FUZZ_BIN) -runs=500 $$tmpdir >/dev/null 2>&1; \
		rm -rf $$tmpdir; \
		echo "fuzz smoke passed"; \
	else \
		echo "fuzz binary not built; skipping smoke run"; \
	fi

clean:
	rm -f $(CAT_HOOK_BIN) $(CAT_ASAN_BIN) $(CAT_ASAN_HOOK_BIN) $(FUZZ_BIN)
	rm -rf ./tmp
