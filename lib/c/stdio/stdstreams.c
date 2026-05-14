#include <stdio.h>
#include <unistd.h>

FILE *stdin;
FILE *stdout;
FILE *stderr;

/*
 * __stdio_init runs as a libc constructor BEFORE any downstream DSO
 * gets its own initializers.  In particular libstdc++.so.6's
 * cout/cin/cerr/clog constructors (priority 99 in globals_io.cc)
 * fdopen() stdout/stderr at libstdc++-load-time, well before crt0's
 * _start would otherwise call __stdio_init explicitly.  Without
 * this attribute those ctors saw stdout==NULL and wired cout to a
 * null FILE*, so the first `std::cout << x` from main crashed in
 * fwrite at `mov 0x8(%eax), %eax` (the FILE*->fd load) with eax=0.
 *
 * Priority < 99 ensures we beat libstdc++'s priority-99 initializer.
 * Numbers 0..100 are reserved by GCC for the implementation;
 * substrate libc IS the implementation, so taking 50 is appropriate.
 *
 * The DT_INIT call from crt0 still happens but is now a no-op past
 * the first time — fdopen sets the FILE* and __stdio_init is
 * idempotent if called again.
 */
__attribute__((constructor))
void __stdio_init(void) {
	if (!stdin)  stdin  = fdopen(0, "r");
	if (!stdout) stdout = fdopen(1, "w");
	if (!stderr) stderr = fdopen(2, "w");
	if(stdin) stdin->mode = _IOLBF;  // Line buffered input
	if(stdout) stdout->mode = isatty(1) ? _IOLBF : _IOFBF;
	if(stderr) stderr->mode = _IONBF; // Unbuffered error
}
