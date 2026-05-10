PROG = echo
SRCS = echo.c echo_opts.c echo_escape.c echo_write.c

# Dynamic-link against /sbin/ld.so + libc.so.0.  echo is small,
# self-contained, and not bootstrap-essential — first canary for
# the dynamic-userland transition.
DYNAMIC = 1

include ../../Makefile.inc
include ../../Makefile.bin.inc
