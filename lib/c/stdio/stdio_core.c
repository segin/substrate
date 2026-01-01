#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define BUFSIZ 1024

// Helper to flush buffer
static int __fflush_write(FILE *f) {
    if (f->pos > f->buffer) {
        size_t len = f->pos - f->buffer;
        ssize_t written = write(f->fd, f->buffer, len);
        if (written < 0) {
            f->error = 1;
            return EOF;
        }
        f->pos = f->buffer;
    }
    return 0;
}

FILE *fdopen(int fd, const char *mode) {
    if (fd < 0) return NULL;
    
    FILE *f = calloc(1, sizeof(FILE));
    if (!f) return NULL;
    
    f->fd = fd;
    f->own_buffer = 1;
    f->buffer = malloc(BUFSIZ);
    if (!f->buffer) {
        free(f);
        return NULL;
    }
    f->buf_end = f->buffer + BUFSIZ;
    f->pos = f->buffer;
    f->limit = f->buffer; // For read: 0 data; For write: buffer end (handled differently) 
    
    // Parse mode
    int flags = 0;
    if (strchr(mode, '+')) flags = O_RDWR;
    else if (strchr(mode, 'r')) flags = O_RDONLY;
    else flags = O_WRONLY; // w/a implied
    
    f->flags = flags; // Simplified
    
    // Check if isatty
    // if (isatty(fd)) f->mode = _IOLBF; else f->mode = _IOFBF;
    f->mode = _IOFBF; 
    
    return f;
}

FILE *fopen(const char *path, const char *mode) {
    int flags = 0;
    int acc_mode = 0666;
    
    const char *p = mode;
    int rw = 0;
    int create = 0;
    int trunc = 0;
    int append = 0;
    
    if (*p == 'r') { rw = O_RDONLY; } 
    else if (*p == 'w') { rw = O_WRONLY; create = O_CREAT; trunc = O_TRUNC; } 
    else if (*p == 'a') { rw = O_WRONLY; create = O_CREAT; append = O_APPEND; } 
    else return NULL;
    
    if (strchr(mode, '+')) rw = O_RDWR;
    
    flags = rw | create | trunc | append;
    
    int fd = open(path, flags, acc_mode);
    if (fd < 0) return NULL;
    
    return fdopen(fd, mode);
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    fflush(stream);
    close(stream->fd);
    if (stream->own_buffer) free(stream->buffer);
    free(stream);
    return 0;
}

int fflush(FILE *stream) {
    if (!stream) return 0; // TODO: fflush(NULL) should flush all
    if (stream->flags == O_RDONLY) {
        // Purge read buffer
        stream->pos = stream->buffer;
        stream->limit = stream->buffer;
        return 0;
    } else {
        return __fflush_write(stream);
    }
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    size_t read_bytes = 0;
    unsigned char *dest = ptr;
    
    if (stream->has_unget) {
        *dest++ = stream->unget_char;
        stream->has_unget = 0;
        read_bytes++;
        total--;
    }
    
    while (total > 0) {
        // Available in buffer
        size_t avail = stream->limit - stream->pos;
        if (avail > 0) {
            size_t copy = (avail < total) ? avail : total;
            memcpy(dest, stream->pos, copy);
            stream->pos += copy;
            dest += copy;
            read_bytes += copy;
            total -= copy;
        } else {
            // Refill
            if (total >= BUFSIZ) {
                // Read directly if request is large
                ssize_t ret = read(stream->fd, dest, total);
                if (ret <= 0) {
                    if (ret == 0) stream->eof = 1; else stream->error = 1;
                    break;
                }
                dest += ret;
                read_bytes += ret;
                total -= ret;
            } else {
                ssize_t ret = read(stream->fd, stream->buffer, BUFSIZ);
                if (ret <= 0) {
                    if (ret == 0) stream->eof = 1; else stream->error = 1;
                    break;
                }
                stream->pos = stream->buffer;
                stream->limit = stream->buffer + ret;
            }
        }
    }
    return read_bytes / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    size_t written = 0;
    const unsigned char *src = ptr;
    
    if (stream->mode == _IONBF) {
        ssize_t ret = write(stream->fd, ptr, total);
        if (ret >= 0) return ret / size;
        stream->error = 1;
        return 0;
    }
    
    while (total > 0) {
        size_t room = stream->buf_end - stream->pos;
        if (total < room) {
            memcpy(stream->pos, src, total);
            stream->pos += total;
            src += total;
            written += total;
            total = 0;
            // Check line buffering
            if (stream->mode == _IOLBF && memchr(stream->pos - written, '\n', written)) {
                __fflush_write(stream);
            }
        } else {
            // Buffer full, flush
            memcpy(stream->pos, src, room);
            stream->pos += room;
            src += room;
            written += room;
            total -= room;
            if (__fflush_write(stream) == EOF) break;
        }
    }
    return written / size;
}

int fseek(FILE *stream, long offset, int whence) {
    fflush(stream);
    // Reset internal pointers
    stream->pos = stream->limit = stream->buffer;
    stream->has_unget = 0;
    stream->eof = 0;
    
    off_t ret = lseek(stream->fd, offset, whence); // Need lseek wrapper in unistd
    if (ret == -1) {
        stream->error = 1;
        return -1;
    }
    return 0;
}

long ftell(FILE *stream) {
    return lseek(stream->fd, 0, SEEK_CUR); // Doesn't account for buffer! 
    // Correct impl needs to subtract (limit - pos) for read, or add (pos - buffer) for write
}

void rewind(FILE *stream) {
    fseek(stream, 0, SEEK_SET);
    clearerr(stream);
}

int fgetc(FILE *stream) {
    unsigned char c;
    if (fread(&c, 1, 1, stream) == 1) return c;
    return EOF;
}

int fputc(int c, FILE *stream) {
    unsigned char ch = c;
    if (fwrite(&ch, 1, 1, stream) == 1) return c;
    return EOF;
}

int ungetc(int c, FILE *stream) {
    if (c == EOF) return EOF;
    if (stream->has_unget) return EOF;
    stream->unget_char = c;
    stream->has_unget = 1;
    stream->eof = 0;
    return c;
}

void clearerr(FILE *stream) {
    stream->error = 0;
    stream->eof = 0;
}

int feof(FILE *stream) { return stream->eof; }
int ferror(FILE *stream) { return stream->error; }

// Wrappers
int getc(FILE *stream) { return fgetc(stream); }
int putc(int c, FILE *stream) { return fputc(c, stream); }
int getchar(void) { return fgetc(stdin); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *stream) {
    return fwrite(s, 1, strlen(s), stream);
}

int puts(const char *s) {
    if (fputs(s, stdout) == EOF) return EOF;
    return putchar('\n');
}

char *fgets(char *s, int size, FILE *stream) {
    if (size <= 0) return NULL;
    char *p = s;
    size--;
    int c;
    while (size > 0 && (c = fgetc(stream)) != EOF) {
        *p++ = (char)c;
        if (c == '\n') break;
        size--;
    }
    if (p == s) return NULL;
    *p = 0;
    return s;
}

void perror(const char *s) {
    if (s && *s) fprintf(stderr, "%s: error %d\n", s, errno);
    else fprintf(stderr, "error %d\n", errno);
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) return -1;
    if (*lineptr == NULL || *n == 0) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) return -1;
    }

    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            size_t new_n = *n * 2;
            char *new_ptr = realloc(*lineptr, new_n);
            if (!new_ptr) return -1;
            *lineptr = new_ptr;
            *n = new_n;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == '\n') break;
    }
    if (pos == 0) return -1;
    (*lineptr)[pos] = 0;
    return (ssize_t)pos;
}

int remove(const char *pathname) { return unlink(pathname); }
