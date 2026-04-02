CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -g

INCLUDES = -I../../../include -I../../../sys/include -I../../../usr.lib/exvi
LIBEXVI = ../../../usr.lib/exvi/libexvi.a

.PHONY: all clean run libexvi_native

all: test_exvi_core test_exvi_prop

test_exvi_core: test_main.o libexvi_native
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_main.o $(LIBEXVI)

test_exvi_prop: test_property.o libexvi_native
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_property.o $(LIBEXVI)

libexvi_native:
	$(MAKE) -C ../../../usr.lib/exvi NATIVE_BUILD=1

test_main.o: test_main.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

test_property.o: test_property.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

run: test_exvi_core test_exvi_prop
	./test_exvi_core
	./test_exvi_prop

clean:
	rm -f test_main.o test_property.o test_exvi_core test_exvi_prop
