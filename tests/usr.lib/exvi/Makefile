CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -g

INCLUDES = -I../../../include -I../../../sys/include -I../../../usr.lib/exvi
LIBEXVI = ../../../usr.lib/exvi/libexvi.a

.PHONY: all clean run libexvi_native

all: test_exvi_core test_exvi_prop test_vi_record_replace_char test_exvi_visual

test_exvi_core: test_main.o libexvi_native
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_main.o $(LIBEXVI)

test_exvi_prop: test_property.o libexvi_native
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_property.o $(LIBEXVI)

test_vi_record_replace_char: test_vi_record_replace_char.o libexvi_native
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_vi_record_replace_char.o $(LIBEXVI)

test_exvi_visual: test_visual.o libexvi_native
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_visual.o $(LIBEXVI)

libexvi_native:
	$(MAKE) -C ../../../usr.lib/exvi NATIVE_BUILD=1

test_main.o: test_main.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

test_property.o: test_property.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

test_vi_record_replace_char.o: test_vi_record_replace_char.c
	$(CC) $(CFLAGS) -Wno-unused-result -idirafter ../../../include -idirafter ../../../sys/include -I../../../usr.lib/exvi -c $< -o $@

test_visual.o: test_visual.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

run: test_exvi_core test_exvi_prop test_vi_record_replace_char test_exvi_visual
	./test_exvi_core
	./test_exvi_prop
	./test_vi_record_replace_char
	./test_exvi_visual

clean:
	rm -f test_main.o test_property.o test_vi_record_replace_char.o test_visual.o test_exvi_core test_exvi_prop test_vi_record_replace_char test_exvi_visual
