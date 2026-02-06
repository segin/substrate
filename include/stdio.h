#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>

#define EOF (-1)

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define BUFSIZ 1024
#define L_tmpnam 20
#define FILENAME_MAX 256
#define FOPEN_MAX 16
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
typedef long fpos_t;

typedef struct FILE {
    int fd;
    int flags;     // RW, APPEND, etc.
    int mode;      // Buffering mode
    int error;
    int eof;
    
    unsigned char *buffer;     // Start of buffer
    unsigned char *buf_end;    // End of allocated buffer
    unsigned char *pos;        // Current read/write position
    unsigned char *limit;      // End of valid data (read) or buffer end (write)
    
    int own_buffer; // 1 if malloc'd
    
    int unget_char; // Single char pushback (simplified)
    int has_unget;

    struct FILE *next;
    struct FILE *prev;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vprintf(const char *format, va_list ap);
int vfprintf(FILE *stream, const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int vasprintf(char **strp, const char *format, va_list ap);
int asprintf(char **strp, const char *format, ...);

int scanf(const char *format, ...);
int fscanf(FILE *stream, const char *format, ...);
int sscanf(const char *str, const char *format, ...);
int vscanf(const char *format, va_list ap);
int vfscanf(FILE *stream, const char *format, va_list ap);
int vsscanf(const char *str, const char *format, va_list ap);

int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int getc(FILE *stream);
int getchar(void);
int putc(int c, FILE *stream);
int putchar(int c);
int puts(const char *s);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
int ungetc(int c, FILE *stream);

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
int fclose(FILE *stream);
int fflush(FILE *stream);
void setbuf(FILE *stream, char *buf);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void perror(const char *s);

int remove(const char *pathname);
int rename(const char *oldpath, const char *newpath);
FILE *tmpfile(void);
char *tmpnam(char *s);

#endif
