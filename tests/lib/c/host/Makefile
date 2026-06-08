# Host validation of substrate's vsnprintf against the host libc snprintf.
# Built -m32 so va_list / type widths match the i386 substrate ABI.
test_printf: test_printf.c ../../../../lib/c/stdio/printf.c
	cc -m32 -O2 -o $@ test_printf.c
check: test_printf
	./test_printf
clean:
	rm -f test_printf
.PHONY: check clean
