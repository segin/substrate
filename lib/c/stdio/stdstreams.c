#include <stdio.h>
#include <unistd.h>

FILE *stdin;
FILE *stdout;
FILE *stderr;

void __stdio_init(void) {
    stdin = fdopen(0, "r");
    stdout = fdopen(1, "w");
    stderr = fdopen(2, "w");
    if (stdout) stdout->mode = _IOLBF; // Line buffered output
    if (stderr) stderr->mode = _IONBF; // Unbuffered error
}
