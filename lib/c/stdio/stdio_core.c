#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdatomic.h>

static FILE *g_file_list_head = NULL;

/*
 * LIBC-05: the global open-FILE list (g_file_list_head) is shared mutable
 * state — fdopen/fclose splice nodes in and out while fflush(NULL) walks it.
 * Guard it with a single spinlock mirroring libc's malloc lock (CAS +
 * sched_yield) so the C library stays independent of libpthread and is safe
 * to use from processes that don't link it.
 */
extern int sched_yield(void);
static atomic_int __file_list_lock = 0;

static void __file_list_lock_acquire(void) {
	int expected;
	for (;;) {
		expected = 0;
		if (atomic_compare_exchange_weak(&__file_list_lock, &expected, 1)) return;
		sched_yield();
	}
}

static void __file_list_lock_release(void) {
	atomic_store(&__file_list_lock, 0);
}

/* FILE::rw_state — which direction the buffer currently holds data for.  Only
 * a WRITE-buffered stream may be written back by fflush()/__fflush_write(). */
#define _IO_RW_NONE  0
#define _IO_RW_READ  1
#define _IO_RW_WRITE 2

/*
 * Stdio's internal read/write retry on EINTR.  POSIX read(2)/write(2)
 * may return -1 with EINTR when a signal arrives mid-call; the public
 * read/write surface that to callers, but stdio operations want to
 * be transparent to signals (otherwise fread sometimes returns 0
 * with errno=EINTR for no apparent reason).
 */
static ssize_t stdio_read(int fd, void *buf, size_t n) {
    while (1) {
        ssize_t r = read(fd, buf, n);
        if (r < 0 && errno == EINTR) continue;
        return r;
    }
}

static ssize_t stdio_write(int fd, const void *buf, size_t n) {
    while (1) {
        ssize_t r = write(fd, buf, n);
        if (r < 0 && errno == EINTR) continue;
        return r;
    }
}

// Helper to flush buffer
static int __fflush_write(FILE *f) {
	if(f->pos > f->buffer) {
		size_t len = f->pos - f->buffer;
		unsigned char *p = f->buffer;
		while(len > 0) {
			ssize_t written = stdio_write(f->fd, p, len);
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
    
    f->mode = _IOFBF; 
    
    // Add to global list (LIBC-05: guard the shared list)
    __file_list_lock_acquire();
    f->next = g_file_list_head;
    f->prev = NULL;
    if (g_file_list_head) {
        g_file_list_head->prev = f;
    }
    g_file_list_head = f;
    __file_list_lock_release();

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
    /* LIBC-08: a write error at close-time must be reported, not swallowed.
     * Capture the flush and close results and fold them into the return. */
    int flush_err = fflush(stream);

    // Remove from global list (LIBC-05: guard the shared list)
    __file_list_lock_acquire();
    if (stream->prev) {
        stream->prev->next = stream->next;
    } else {
        g_file_list_head = stream->next;
    }
    if (stream->next) {
        stream->next->prev = stream->prev;
    }
    __file_list_lock_release();

    int close_err = close(stream->fd);
    if (stream->own_buffer) free(stream->buffer);
    free(stream);
    return (flush_err == EOF || close_err < 0) ? EOF : 0;
}

int fflush(FILE *stream) {
	if(!stream) {
		/* LIBC-05: walk the shared open-FILE list under the lock so a
		 * concurrent fdopen/fclose can't splice a node mid-traversal. */
		int ret = 0;
		__file_list_lock_acquire();
		FILE *current = g_file_list_head;
		while(current) {
			if(fflush(current) == EOF) ret = EOF;
			current = current->next;
		}
		__file_list_lock_release();
		return ret;
	}
	/* Only write-flush a stream whose buffer actually holds WRITE data.  For a
	 * read-buffered or idle stream — including an update-mode "r+" stream last
	 * used for reading — the buffer holds data read FROM the file, and the old
	 * unconditional __fflush_write() wrote that read-ahead back, corrupting the
	 * file (e.g. fopen("f","r+"); fgetc(f); fclose(f)). */
	if(stream->rw_state != _IO_RW_WRITE) {
		stream->pos = stream->buffer;
		stream->limit = stream->buffer;
		return 0;
	}
	int r = __fflush_write(stream);
	stream->rw_state = _IO_RW_NONE;
	return r;
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
    /* C99 §7.21.8.1: if size or nmemb is zero, fread returns zero and
     * the stream is unchanged.  Guard this BEFORE the trailing
     * `read_bytes / size` — without it, size==0 divides by zero
     * (SIGFPE).  fwrite already had the size==0 guard; fread didn't. */
    if (size == 0 || nmemb == 0) return 0;
    /* Overflow check before multiply — see fwrite for rationale. */
    if (nmemb > SIZE_MAX / size) {
        stream->error = 1;
        return 0;
    }
    size_t total = size * nmemb;
    size_t read_bytes = 0;
    unsigned char *dest = ptr;

    /* The buffer now holds (or will hold) READ data; a subsequent fflush()
     * must not write it back to the file. */
    stream->rw_state = _IO_RW_READ;

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
            /* POSIX-2017 §7.21.5.3: when an input operation on a
             * tty-backed stream needs unbuffered data, every
             * line-buffered, tty-backed output stream must be
             * flushed first.  Without this, prompts like
             *   printf("foo> "); fgets(line, ..., stdin);
             * appear to hang — the prompt is sitting in stdout's
             * line buffer waiting for a '\n' that won't come until
             * the user has typed and we've returned.  Limit the
             * flush to the stdout/stderr pair that's likely to
             * carry a prompt, and only when the input stream is
             * a tty (matches glibc behavior).  */
            if (isatty(stream->fd)) {
                if (stdout && stdout != stream &&
                    stdout->mode == _IOLBF) fflush(stdout);
                if (stderr && stderr != stream &&
                    stderr->mode == _IOLBF) fflush(stderr);
            }
            // Refill
            if (total >= BUFSIZ) {
                // Read directly if request is large
                ssize_t ret = stdio_read(stream->fd, dest, total);
                if (ret <= 0) {
                    if (ret == 0) stream->eof = 1; else stream->error = 1;
                    break;
                }
                dest += ret;
                read_bytes += ret;
                total -= ret;
            } else {
                ssize_t ret = stdio_read(stream->fd, stream->buffer, BUFSIZ);
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
    /* POSIX: "If size or nitems is 0, fwrite() shall return zero and the
     * state of the stream remains unchanged."  Also guards the divisions
     * below against SIGFPE when size=0. */
    if (size == 0 || nmemb == 0) return 0;
    /* Reject overflow before computing total — hostile callers can pass
     * size*nmemb that wraps to a small positive value on 32-bit. */
    if (nmemb > SIZE_MAX / size) {
        stream->error = 1;
        return 0;
    }
    size_t total = size * nmemb;
    size_t written = 0;
    const unsigned char *src = ptr;

    if (stream->mode == _IONBF) {
        /* Loop over short writes — a single write() may transfer fewer
         * bytes than requested (signal, pipe capacity, etc.).  The old
         * code returned ret/size after one call, silently dropping the
         * remainder and under-reporting the element count. */
        size_t off = 0;
        while (off < total) {
            ssize_t ret = stdio_write(stream->fd, src + off, total - off);
            if (ret > 0) {
                off += (size_t)ret;
            } else if (ret < 0 && errno == EINTR) {
                continue;
            } else {
                if (ret < 0) stream->error = 1;
                break;
            }
        }
        return off / size;
    }

    /* Buffered write: the buffer now holds WRITE data that fflush must write
     * back to the file. */
    stream->rw_state = _IO_RW_WRITE;

    while (total > 0) {
        size_t room = stream->buf_end - stream->pos;
        if (total < room) {
            unsigned char *dst_start = stream->pos;
            memcpy(dst_start, src, total);
            stream->pos += total;
            src += total;
            written += total;
            /* Line-buffer flush: search the bytes JUST written, not the
             * cumulative range — `written` accumulates across iterations
             * and would walk past the buffer start. */
            if (stream->mode == _IOLBF && memchr(dst_start, '\n', total)) {
                __fflush_write(stream);
            }
            total = 0;
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
	/* SEEK_CUR must compensate for read-ahead buffering: after an
	 * fread, the kernel's file position is N bytes past the caller's
	 * logical position, where N = (limit - pos) bytes still buffered
	 * (plus one for ungetc).  Compute the compensation BEFORE fflush
	 * — fflush on a read stream resets pos/limit to the buffer base
	 * (per the POSIX 2017 input-fflush contract this is incomplete,
	 * but it's what we do today) which destroys the pending count.
	 * Without this adjustment, fseek(-16, SEEK_CUR) right after
	 * reading 8 bytes lseeks the fd by -16 from its already-advanced
	 * position rather than from the caller's view, landing far past
	 * where the caller expects.  BFD's bfd_generic_archive_p hits
	 * this exact pattern when probing ar archives — reads the 8-byte
	 * magic, reads 16 more for the first member header, then seeks
	 * back -16 — and silently sees garbage if the seek lands inside
	 * the first member's content. */
	long pending = 0;
	if (whence == SEEK_CUR) {
		pending = stream->limit - stream->pos;
		if (stream->has_unget) pending++;
	}

	fflush(stream);

	if (whence == SEEK_CUR)
		offset -= pending;

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

	int access_mode = stream->flags & O_ACCMODE;
	if(access_mode == O_RDONLY || access_mode == O_RDWR) {
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
	/* Same buffer-compensation rationale as fseek() above — compute
	 * pending BEFORE fflush. */
	off_t pending = 0;
	if (whence == SEEK_CUR) {
		pending = (off_t)(stream->limit - stream->pos);
		if (stream->has_unget) pending++;
	}

	fflush(stream);

	if (whence == SEEK_CUR)
		offset -= pending;

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

	int access_mode = stream->flags & O_ACCMODE;
	if(access_mode == O_RDONLY || access_mode == O_RDWR) {
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
#undef fileno
int fileno(FILE *stream) { return stream ? stream->fd : -1; }

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

/* Reentrant tmpnam(3): unlike tmpnam(NULL), tmpnam_r(NULL) returns NULL rather
 * than using a shared static buffer.  The caller's buffer must hold at least
 * L_tmpnam bytes. */
char *tmpnam_r(char *s) {
	static int counter = 0;
	if (s == NULL)
		return NULL;
	snprintf(s, L_tmpnam, "/tmp/tmp.%d.%d", getpid(), counter++);
	return s;
}

/* tempnam(3): malloc a unique temp-file name.  Directory preference: the
 * `dir` argument, else $TMPDIR, else P_tmpdir.  pfx (<= 5 chars) prefixes the
 * basename.  Caller free()s the result. */
char *tempnam(const char *dir, const char *pfx) {
	const char *d = dir;
	if (!d || access(d, W_OK) != 0) {
		d = getenv("TMPDIR");
		if (!d || access(d, W_OK) != 0)
			d = P_tmpdir;
	}
	if (!pfx)
		pfx = "tmp";
	static int counter = 0;
	for (int i = 0; i < 1000; i++) {
		size_t len = strlen(d) + 1 + strlen(pfx) + 32;
		char *name = malloc(len);
		if (!name)
			return NULL;
		snprintf(name, len, "%s/%s%d.%d", d, pfx, getpid(), counter++);
		if (access(name, F_OK) != 0)
			return name;          /* unused name */
		free(name);
	}
	return NULL;
}

/* --- popen / pclose --- */
#include <sys/wait.h>

#define POPEN_TABLE_SIZE 16
static struct { FILE *fp; pid_t pid; } popen_table[POPEN_TABLE_SIZE];

FILE *popen(const char *command, const char *type) {
    int pipefd[2];
    if (pipe(pipefd) < 0)
        return NULL;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }

    int is_read = (type[0] == 'r');

    if (pid == 0) {
        /* child */
        if (is_read) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
        } else {
            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
        }
        const char *argv[4];
        argv[0] = "/bin/sh";
        argv[1] = "-c";
        argv[2] = command;
        argv[3] = NULL;
        execv("/bin/sh", (char *const *)argv);
        /* execv only returns on failure.  Surface a diagnostic so the
         * caller knows the child died without ever running the command,
         * rather than seeing an instant EOF from the pipe. */
        const char *msg = "popen: failed to exec /bin/sh\n";
        size_t mlen = 0;
        while (msg[mlen]) mlen++;
        (void)write(STDERR_FILENO, msg, mlen);
        _exit(127);
    }

    /* parent */
    FILE *fp;
    if (is_read) {
        close(pipefd[1]);
        fp = fdopen(pipefd[0], "r");
        if (!fp) { close(pipefd[0]); goto err; }
    } else {
        close(pipefd[0]);
        fp = fdopen(pipefd[1], "w");
        if (!fp) { close(pipefd[1]); goto err; }
    }

    for (int i = 0; i < POPEN_TABLE_SIZE; i++) {
        if (popen_table[i].fp == NULL) {
            popen_table[i].fp = fp;
            popen_table[i].pid = pid;
            return fp;
        }
    }
    fclose(fp);
err:
    waitpid(pid, NULL, 0);
    return NULL;
}

int pclose(FILE *stream) {
    for (int i = 0; i < POPEN_TABLE_SIZE; i++) {
        if (popen_table[i].fp == stream) {
            pid_t pid = popen_table[i].pid;
            popen_table[i].fp = NULL;
            popen_table[i].pid = 0;
            fclose(stream);
            int status = 0;
            if (waitpid(pid, &status, 0) < 0)
                return -1;
            /* LIBC-07: POSIX requires pclose() to return the raw wait status
             * (as from waitpid), not the decoded exit code — callers apply
             * WIFEXITED/WEXITSTATUS/WIFSIGNALED themselves. */
            return status;
        }
    }
    return -1;
}
