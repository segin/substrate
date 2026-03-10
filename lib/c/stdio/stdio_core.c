#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

static FILE *g_file_list_head = NULL;

// Helper to flush buffer
static int __fflush_write(FILE *f) {
	if(f->pos > f->buffer) {
		size_t len = f->pos - f->buffer;
		unsigned char *p = f->buffer;
		while(len > 0) {
			ssize_t written = write(f->fd, p, len);
			if(written < 0) {
				f->error = 1;
				return EOF;
			}
			p += written;
			len -= written;
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
    
    // Add to global list
    f->next = g_file_list_head;
    f->prev = NULL;
    if (g_file_list_head) {
        g_file_list_head->prev = f;
    }
    g_file_list_head = f;

    return f;
}

FILE *fopen(const char *path, const char *mode) {
	int flags = 0;
	int acc_mode = 0666;
	int rw = 0, create = 0, trunc = 0, append = 0, excl = 0;

	if(mode[0] == 'r') { rw = O_RDONLY; }
	else if(mode[0] == 'w') { rw = O_WRONLY; create = O_CREAT; trunc = O_TRUNC; }
	else if(mode[0] == 'a') { rw = O_WRONLY; create = O_CREAT; append = O_APPEND; }
	else return NULL;

	if(strchr(mode, '+')) rw = O_RDWR;
	if(strchr(mode, 'x')) excl = O_EXCL; // C11 exclusive create

	flags = rw | create | trunc | append | excl;

	int fd = open(path, flags, acc_mode);
	if(fd < 0) return NULL;

	return fdopen(fd, mode);
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    fflush(stream);

    // Remove from global list
    if (stream->prev) {
        stream->prev->next = stream->next;
    } else {
        g_file_list_head = stream->next;
    }
    if (stream->next) {
        stream->next->prev = stream->prev;
    }

    close(stream->fd);
    if (stream->own_buffer) free(stream->buffer);
    free(stream);
    return 0;
}

int fflush(FILE *stream) {
	if(!stream) {
		int ret = 0;
		FILE *current = g_file_list_head;
		while(current) {
			if(fflush(current) == EOF) ret = EOF;
			current = current->next;
		}
		return ret;
	}
	if(stream->flags == O_RDONLY) {
		stream->pos = stream->buffer;
		stream->limit = stream->buffer;
		return 0;
	}
	return __fflush_write(stream);
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
	if(!stream) return -1;
	if(mode != _IOFBF && mode != _IOLBF && mode != _IONBF) return -1;

	fflush(stream);

	if(mode == _IONBF) {
		stream->mode = _IONBF;
		return 0;
	}

	if(buf) {
		if(stream->own_buffer) free(stream->buffer);
		stream->buffer = (unsigned char *)buf;
		stream->buf_end = (unsigned char *)buf + size;
		stream->own_buffer = 0;
	} else if(size > 0 && size != (size_t)(stream->buf_end - stream->buffer)) {
		unsigned char *newbuf = malloc(size);
		if(!newbuf) return -1;
		if(stream->own_buffer) free(stream->buffer);
		stream->buffer = newbuf;
		stream->buf_end = newbuf + size;
		stream->own_buffer = 1;
	}
	stream->pos = stream->buffer;
	stream->limit = stream->buffer;
	stream->mode = mode;
	return 0;
}

void setbuf(FILE *stream, char *buf) {
	setvbuf(stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

void setlinebuf(FILE *stream) {
	setvbuf(stream, NULL, _IOLBF, 0);
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
	if(!stream) return NULL;

	fflush(stream);
	close(stream->fd);

	if(!path) {
		// C99: change mode of existing fd — not fully implementable without fcntl
		return NULL;
	}

	int flags = 0;
	int rw = 0, create = 0, trunc = 0, append = 0, excl = 0;

	if(mode[0] == 'r') { rw = O_RDONLY; }
	else if(mode[0] == 'w') { rw = O_WRONLY; create = O_CREAT; trunc = O_TRUNC; }
	else if(mode[0] == 'a') { rw = O_WRONLY; create = O_CREAT; append = O_APPEND; }
	else return NULL;

	if(strchr(mode, '+')) rw = O_RDWR;
	if(strchr(mode, 'x')) excl = O_EXCL;

	flags = rw | create | trunc | append | excl;

	int fd = open(path, flags, 0666);
	if(fd < 0) return NULL;

	stream->fd = fd;
	stream->flags = rw;
	stream->error = 0;
	stream->eof = 0;
	stream->pos = stream->buffer;
	stream->limit = stream->buffer;
	stream->has_unget = 0;

	return stream;
}

int fcloseall(void) {
	int ret = 0;
	FILE *current = g_file_list_head;
	while(current) {
		FILE *next = current->next;
		if(fclose(current) == EOF) ret = EOF;
		current = next;
	}
	return ret;
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
	stream->pos = stream->limit = stream->buffer;
	stream->has_unget = 0;
	stream->eof = 0;

	off_t ret = lseek(stream->fd, offset, whence);
	if(ret == -1) {
		stream->error = 1;
		return -1;
	}
	return 0;
}

long ftell(FILE *stream) {
	long pos = lseek(stream->fd, 0, SEEK_CUR);
	if(pos == -1) return -1;

	if(stream->flags == O_RDONLY || (stream->flags & O_RDWR)) {
		// Read mode: subtract unread buffered data
		pos -= (stream->limit - stream->pos);
		if(stream->has_unget) pos--;
	} else {
		// Write mode: add buffered but unflushed data
		pos += (stream->pos - stream->buffer);
	}
	return pos;
}

int fgetpos(FILE *stream, fpos_t *pos) {
	long p = ftell(stream);
	if(p == -1) return -1;
	*pos = (fpos_t)p;
	return 0;
}

int fsetpos(FILE *stream, const fpos_t *pos) {
	return fseek(stream, (long)*pos, SEEK_SET);
}

int fseeko(FILE *stream, off_t offset, int whence) {
	fflush(stream);
	stream->pos = stream->limit = stream->buffer;
	stream->has_unget = 0;
	stream->eof = 0;

	off_t ret = lseek(stream->fd, offset, whence);
	if(ret == -1) {
		stream->error = 1;
		return -1;
	}
	return 0;
}

off_t ftello(FILE *stream) {
	off_t pos = lseek(stream->fd, 0, SEEK_CUR);
	if(pos == -1) return -1;

	if(stream->flags == O_RDONLY || (stream->flags & O_RDWR)) {
		pos -= (stream->limit - stream->pos);
		if(stream->has_unget) pos--;
	} else {
		pos += (stream->pos - stream->buffer);
	}
	return pos;
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
	size_t len = strlen(s);
	if(fwrite(s, 1, len, stream) != len) return EOF;
	return 0;
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

FILE *tmpfile(void) {
	static int tmpfile_counter = 0;
	char name[L_tmpnam + 16];

	for(int i = 0; i < 100; i++) {
		snprintf(name, sizeof(name), "/tmp/.tmpf.%d.%d", getpid(), tmpfile_counter++);
		int fd = open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if(fd >= 0) {
			unlink(name); // Delete on close
			FILE *f = fdopen(fd, "w+");
			if(!f) { close(fd); return NULL; }
			return f;
		}
	}
	return NULL;
}

char *tmpnam(char *s) {
	static char buf[L_tmpnam];
	static int counter = 0;
	char *p = s ? s : buf;
	snprintf(p, L_tmpnam, "/tmp/tmp.%d.%d", getpid(), counter++);
	return p;
}
