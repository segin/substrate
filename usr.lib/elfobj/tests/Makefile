include ../../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../../include -I../../../sys -I../src

TESTS = test_roundtrip test_invalid test_link

.PHONY: all clean

all: $(TESTS)
	./test_roundtrip
	./test_invalid
	./test_link

%: %.c ../libelfobj.a
	$(CC) $(CFLAGS) $< ../libelfobj.a -o $@

clean:
	rm -f $(TESTS) *.o tmp_*.o tmp_*.elf
