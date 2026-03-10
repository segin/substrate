#include <stdio.h>
#include <unistd.h>

FILE *stdin;
FILE *stdout;
FILE *stderr;

void __stdio_init(void) {
	stdin = fdopen(0, "r");
	stdout = fdopen(1, "w");
	stderr = fdopen(2, "w");
	if(stdin) stdin->mode = _IOLBF;  // Line buffered input
	if(stdout) stdout->mode = isatty(1) ? _IOLBF : _IOFBF;
	if(stderr) stderr->mode = _IONBF; // Unbuffered error
}
