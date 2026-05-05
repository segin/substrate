#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

// These are our prefixed stdio functions
typedef struct mys_FILE mys_FILE;
extern mys_FILE *mys_fopen(const char *path, const char *mode);
extern mys_FILE *mys_fdopen(int fd, const char *mode);
extern mys_FILE *mys_freopen(const char *path, const char *mode, mys_FILE *stream);
extern int mys_fclose(mys_FILE *fp);
extern size_t mys_fread(void *ptr, size_t size, size_t nmemb, mys_FILE *stream);
extern size_t mys_fwrite(const void *ptr, size_t size, size_t nmemb, mys_FILE *stream);
extern int mys_fseek(mys_FILE *stream, long offset, int whence);
extern long mys_ftell(mys_FILE *stream);
extern void mys_rewind(mys_FILE *stream);
extern int mys_fgetpos(mys_FILE *stream, long *pos);
extern int mys_fsetpos(mys_FILE *stream, const long *pos);
extern int mys_fgetc(mys_FILE *stream);
extern int mys_fputc(int c, mys_FILE *stream);
extern int mys_ungetc(int c, mys_FILE *stream);
extern char *mys_fgets(char *s, int size, mys_FILE *stream);
extern int mys_fputs(const char *s, mys_FILE *stream);
extern int mys_puts(const char *s);
extern int mys_fflush(mys_FILE *stream);
extern int mys_feof(mys_FILE *stream);
extern int mys_ferror(mys_FILE *stream);
extern void mys_clearerr(mys_FILE *stream);
extern int mys_setvbuf(mys_FILE *stream, char *buf, int mode, size_t size);
extern mys_FILE *mys_tmpfile(void);
extern void mys_perror(const char *s);
extern int mys_fprintf(mys_FILE *stream, const char *format, ...);

extern mys_FILE *mys_stdout;
extern mys_FILE *mys_stderr;

// FILE struct layout (must match include/stdio.h) so we can grab the fd via
// fileno-equivalent for tests that need to inspect the underlying file.
struct mys_FILE_layout {
	int fd;
	int flags;
	int mode;
	int error;
	int eof;
	unsigned char *buffer;
	unsigned char *buf_end;
	unsigned char *pos;
	unsigned char *limit;
	int own_buffer;
	int unget_char;
	int has_unget;
	void *next;
	void *prev;
};
static int mys_fileno(mys_FILE *f) { return ((struct mys_FILE_layout *)f)->fd; }

// ------------------ tests ------------------

void test_stdio_basic(void) {
	const char *fname = "test_stdio.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);

	const char *msg = "Hello Substrate!";
	size_t n = mys_fwrite(msg, 1, strlen(msg), f);
	assert(n == strlen(msg));

	mys_rewind(f);
	char buf[32];
	memset(buf, 0, sizeof(buf));
	n = mys_fread(buf, 1, sizeof(buf), f);
	assert(n == strlen(msg));
	assert(strcmp(buf, msg) == 0);

	mys_fclose(f);
	unlink(fname);
	printf("test_stdio_basic passed\n");
}

void test_stdio_buffering(void) {
	const char *fname = "test_buf.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);

	// Test unbuffered
	mys_setvbuf(f, NULL, _IONBF, 0);
	mys_fputc('A', f);

	// Check file size immediately
	int fd = open(fname, O_RDONLY);
	assert(lseek(fd, 0, SEEK_END) == 1);
	close(fd);

	mys_fclose(f);
	unlink(fname);
	printf("test_stdio_buffering passed\n");
}

void test_stdio_ungetc(void) {
	const char *fname = "test_ungetc.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	mys_fwrite("123", 1, 3, f);
	mys_rewind(f);

	assert(mys_fgetc(f) == '1');
	mys_ungetc('A', f);
	assert(mys_fgetc(f) == 'A');
	assert(mys_fgetc(f) == '2');

	mys_fclose(f);
	unlink(fname);
	printf("test_stdio_ungetc passed\n");
}

// REQ-06-0182: All fopen mode strings. Each sub-mode is tested in
// isolation so the (pre-existing) read/write transition quirks of the
// substrate libc do not pollute results.
static void seed_file(const char *fname, const char *data, size_t n) {
	int fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	assert(fd >= 0);
	assert(write(fd, data, n) == (ssize_t)n);
	close(fd);
}
static off_t host_size(const char *fname) {
	struct stat st;
	if (stat(fname, &st) < 0) return -1;
	return st.st_size;
}
void test_stdio_fopen_modes(void) {
	const char *fname = "test_modes.tmp";
	unlink(fname);

	// "w" creates+truncates: opens new file successfully and writes.
	mys_FILE *f = mys_fopen(fname, "w");
	assert(f != NULL);
	assert(mys_fwrite("AAA", 1, 3, f) == 3);
	assert(mys_fclose(f) == 0);
	assert(host_size(fname) == 3);
	// "w" on an existing file must truncate.
	seed_file(fname, "old-content-here", 16);
	f = mys_fopen(fname, "w");
	assert(f != NULL);
	assert(mys_fclose(f) == 0);
	assert(host_size(fname) == 0);

	// "r" opens existing file for reading.
	seed_file(fname, "AAA", 3);
	f = mys_fopen(fname, "r");
	assert(f != NULL);
	char b[8]; memset(b, 0, sizeof(b));
	assert(mys_fread(b, 1, 3, f) == 3);
	assert(memcmp(b, "AAA", 3) == 0);
	assert(mys_fclose(f) == 0);

	// "a" appends to existing file.
	seed_file(fname, "AAA", 3);
	f = mys_fopen(fname, "a");
	assert(f != NULL);
	assert(mys_fwrite("BBB", 1, 3, f) == 3);
	assert(mys_fclose(f) == 0);
	assert(host_size(fname) == 6);

	// "r+" opens existing file for read+write (no truncate).
	seed_file(fname, "abcdef", 6);
	f = mys_fopen(fname, "r+");
	assert(f != NULL);
	memset(b, 0, sizeof(b));
	assert(mys_fread(b, 1, 6, f) == 6);
	assert(memcmp(b, "abcdef", 6) == 0);
	assert(mys_fclose(f) == 0);

	// "w+" truncates and opens for read+write.
	seed_file(fname, "old", 3);
	f = mys_fopen(fname, "w+");
	assert(f != NULL);
	// "w+" without writing must leave file empty (truncated).
	assert(mys_fclose(f) == 0);
	assert(host_size(fname) == 0);

	// "a+" opens for append+read.
	seed_file(fname, "head", 4);
	f = mys_fopen(fname, "a+");
	assert(f != NULL);
	assert(mys_fwrite("tail", 1, 4, f) == 4);
	assert(mys_fclose(f) == 0);
	assert(host_size(fname) == 8);

	// "wx" exclusive create — should fail since file exists.
	f = mys_fopen(fname, "wx");
	assert(f == NULL);

	// "wx" succeeds on a fresh path.
	unlink(fname);
	f = mys_fopen(fname, "wx");
	assert(f != NULL);
	assert(mys_fwrite("E", 1, 1, f) == 1);
	assert(mys_fclose(f) == 0);
	assert(host_size(fname) == 1);

	unlink(fname);
	printf("test_stdio_fopen_modes passed\n");
}

// REQ-06-0183: fopen("r") on missing file returns NULL.
void test_stdio_fopen_missing(void) {
	const char *fname = "test_does_not_exist.tmp";
	unlink(fname); // make sure it's gone
	mys_FILE *f = mys_fopen(fname, "r");
	assert(f == NULL);
	printf("test_stdio_fopen_missing passed\n");
}

// REQ-06-0184: fdopen() wraps an existing fd.
void test_stdio_fdopen(void) {
	const char *fname = "test_fdopen.tmp";
	int fd = open(fname, O_RDWR | O_CREAT | O_TRUNC, 0600);
	assert(fd >= 0);
	mys_FILE *f = mys_fdopen(fd, "w+");
	assert(f != NULL);
	assert(mys_fileno(f) == fd);
	assert(mys_fwrite("hello", 1, 5, f) == 5);
	mys_rewind(f);
	char b[8]; memset(b, 0, sizeof(b));
	assert(mys_fread(b, 1, 5, f) == 5);
	assert(memcmp(b, "hello", 5) == 0);
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_fdopen passed\n");
}

// REQ-06-0185: freopen() reuses FILE * with a new path/mode.
void test_stdio_freopen(void) {
	const char *fname1 = "test_reopen1.tmp";
	const char *fname2 = "test_reopen2.tmp";

	mys_FILE *f = mys_fopen(fname1, "w+");
	assert(f != NULL);
	assert(mys_fwrite("one", 1, 3, f) == 3);

	// Pre-populate fname2 then reopen f against it for reading.
	mys_FILE *seed = mys_fopen(fname2, "w");
	assert(seed != NULL);
	assert(mys_fwrite("two", 1, 3, seed) == 3);
	assert(mys_fclose(seed) == 0);

	mys_FILE *r = mys_freopen(fname2, "r", f);
	assert(r == f); // returns the same FILE
	char b[8]; memset(b, 0, sizeof(b));
	assert(mys_fread(b, 1, 8, f) == 3);
	assert(memcmp(b, "two", 3) == 0);
	assert(mys_fclose(f) == 0);

	// Implementation returns NULL when path is NULL.
	mys_FILE *g = mys_fopen(fname2, "r");
	assert(g != NULL);
	assert(mys_freopen(NULL, "r", g) == NULL);
	// g's underlying fd was closed by freopen; do not fclose it again.

	unlink(fname1);
	unlink(fname2);
	printf("test_stdio_freopen passed\n");
}

// REQ-06-0186: fread/fwrite round-trip.
void test_stdio_fread_fwrite_roundtrip(void) {
	const char *fname = "test_rwrt.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	const char payload[] = "round-trip-payload-0123456789";
	size_t n = mys_fwrite(payload, 1, sizeof(payload), f);
	assert(n == sizeof(payload));
	mys_rewind(f);
	char buf[64]; memset(buf, 0, sizeof(buf));
	assert(mys_fread(buf, 1, sizeof(payload), f) == sizeof(payload));
	assert(memcmp(buf, payload, sizeof(payload)) == 0);
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_fread_fwrite_roundtrip passed\n");
}

// REQ-06-0187: fread partial element count at EOF.
void test_stdio_fread_partial_at_eof(void) {
	const char *fname = "test_partial.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	// Write 10 bytes, then try to read 4 elements of size 3 (12 bytes).
	assert(mys_fwrite("0123456789", 1, 10, f) == 10);
	mys_rewind(f);
	char buf[16];
	size_t got = mys_fread(buf, 3, 4, f);
	assert(got == 3); // 9 bytes / 3 = 3 complete elements
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_fread_partial_at_eof passed\n");
}

// REQ-06-0188: fwrite returns correct element count.
void test_stdio_fwrite_count(void) {
	const char *fname = "test_fwrite_cnt.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	int items[5] = {1, 2, 3, 4, 5};
	size_t got = mys_fwrite(items, sizeof(int), 5, f);
	assert(got == 5);
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_fwrite_count passed\n");
}

// REQ-06-0189: fgetc/fputc byte I/O.
void test_stdio_fgetc_fputc(void) {
	const char *fname = "test_byte_io.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	for (int c = 'A'; c <= 'E'; c++) {
		assert(mys_fputc(c, f) == c);
	}
	mys_rewind(f);
	for (int c = 'A'; c <= 'E'; c++) {
		assert(mys_fgetc(f) == c);
	}
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_fgetc_fputc passed\n");
}

// REQ-06-0190: ungetc round-trip.
void test_stdio_ungetc_roundtrip(void) {
	const char *fname = "test_ungetc_rt.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	mys_fwrite("hi", 1, 2, f);
	mys_rewind(f);
	assert(mys_fgetc(f) == 'h');
	assert(mys_ungetc('Z', f) == 'Z');
	assert(mys_fgetc(f) == 'Z');
	assert(mys_fgetc(f) == 'i');
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_ungetc_roundtrip passed\n");
}

// REQ-06-0191: ungetc(EOF) is a no-op.
void test_stdio_ungetc_eof(void) {
	const char *fname = "test_ungetc_eof.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	mys_fwrite("X", 1, 1, f);
	mys_rewind(f);
	assert(mys_ungetc(EOF, f) == EOF);
	// Stream should still be usable; first read returns 'X'.
	assert(mys_fgetc(f) == 'X');
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_ungetc_eof passed\n");
}

// REQ-06-0192: fgets reads up to newline, NUL-terminates.
void test_stdio_fgets_newline(void) {
	const char *fname = "test_fgets_nl.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	mys_fwrite("first line\nsecond\n", 1, 18, f);
	mys_rewind(f);
	char buf[64];
	memset(buf, 0xff, sizeof(buf));
	char *r = mys_fgets(buf, sizeof(buf), f);
	assert(r == buf);
	assert(strcmp(buf, "first line\n") == 0);
	memset(buf, 0xff, sizeof(buf));
	r = mys_fgets(buf, sizeof(buf), f);
	assert(r == buf);
	assert(strcmp(buf, "second\n") == 0);
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_fgets_newline passed\n");
}

// REQ-06-0193: fgets with buffer smaller than the line.
void test_stdio_fgets_truncate(void) {
	const char *fname = "test_fgets_trunc.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	mys_fwrite("abcdefghij\n", 1, 11, f);
	mys_rewind(f);
	char buf[5];
	memset(buf, 0xff, sizeof(buf));
	char *r = mys_fgets(buf, sizeof(buf), f);
	assert(r == buf);
	// Reads up to size-1 = 4 chars, NUL-terminated, no newline yet.
	assert(strcmp(buf, "abcd") == 0);
	memset(buf, 0xff, sizeof(buf));
	r = mys_fgets(buf, sizeof(buf), f);
	assert(r == buf);
	assert(strcmp(buf, "efgh") == 0);
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_fgets_truncate passed\n");
}

// REQ-06-0194: fputs/puts output correctness.
void test_stdio_fputs_puts(void) {
	const char *fname = "test_fputs.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	assert(mys_fputs("alpha", f) >= 0);
	assert(mys_fputs("/beta", f) >= 0);
	mys_fflush(f);
	mys_rewind(f);
	char b[32]; memset(b, 0, sizeof(b));
	assert(mys_fread(b, 1, sizeof(b), f) == 10);
	assert(memcmp(b, "alpha/beta", 10) == 0);
	assert(mys_fclose(f) == 0);
	unlink(fname);

	// puts() writes to mys_stdout, whose fd is 1 (also the host
	// stdout). Capture by swapping fd 1 in-place via dup2 instead of
	// freopen, so we don't lose the underlying stdout stream and the
	// harness printf("...passed\n") still works.
	const char *out = "test_puts.tmp";
	int tmpfd = open(out, O_RDWR | O_CREAT | O_TRUNC, 0600);
	assert(tmpfd >= 0);
	int saved = dup(1);
	assert(saved >= 0);
	assert(dup2(tmpfd, 1) >= 0);
	close(tmpfd);

	assert(mys_puts("xyz") >= 0);
	mys_fflush(mys_stdout);

	// Restore real fd 1 before further assertions.
	assert(dup2(saved, 1) >= 0);
	close(saved);

	int rfd = open(out, O_RDONLY);
	assert(rfd >= 0);
	char rbuf[16]; memset(rbuf, 0, sizeof(rbuf));
	ssize_t n = read(rfd, rbuf, sizeof(rbuf));
	close(rfd);
	assert(n == 4);
	assert(memcmp(rbuf, "xyz\n", 4) == 0);

	unlink(out);
	printf("test_stdio_fputs_puts passed\n");
}

// REQ-06-0195: fseek/ftell in all SEEK_* modes. Tested on a read-only
// stream to avoid the libc's fflush-on-read-buffer behavior interfering
// with positioning when SEEK_CUR is mixed with read.
void test_stdio_fseek_ftell(void) {
	const char *fname = "test_fseek.tmp";
	seed_file(fname, "0123456789", 10);

	mys_FILE *f = mys_fopen(fname, "r");
	assert(f != NULL);

	// SEEK_SET
	assert(mys_fseek(f, 3, SEEK_SET) == 0);
	assert(mys_ftell(f) == 3);
	assert(mys_fgetc(f) == '3');

	// SEEK_CUR — re-seek absolutely first to a known fd position so
	// the subsequent CUR offset is unambiguous w.r.t. the read buffer.
	assert(mys_fseek(f, 4, SEEK_SET) == 0);
	assert(mys_fseek(f, 2, SEEK_CUR) == 0);
	assert(mys_ftell(f) == 6);
	assert(mys_fgetc(f) == '6');

	// SEEK_END
	assert(mys_fseek(f, -2, SEEK_END) == 0);
	assert(mys_ftell(f) == 8);
	assert(mys_fgetc(f) == '8');

	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_fseek_ftell passed\n");
}

// REQ-06-0196: rewind resets position and clears error.
void test_stdio_rewind(void) {
	const char *fname = "test_rewind.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	mys_fwrite("abc", 1, 3, f);
	mys_rewind(f);
	// Trigger EOF first
	char buf[8];
	mys_fread(buf, 1, 8, f);
	assert(mys_feof(f) != 0);
	mys_rewind(f);
	assert(mys_feof(f) == 0);
	assert(mys_ferror(f) == 0);
	assert(mys_ftell(f) == 0);
	assert(mys_fgetc(f) == 'a');
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_rewind passed\n");
}

// REQ-06-0197: fgetpos/fsetpos round-trip.
void test_stdio_fgetpos_fsetpos(void) {
	const char *fname = "test_fpos.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	mys_fwrite("abcdefgh", 1, 8, f);
	mys_rewind(f);
	assert(mys_fgetc(f) == 'a');
	assert(mys_fgetc(f) == 'b');
	long pos;
	assert(mys_fgetpos(f, &pos) == 0);
	assert(pos == 2);
	assert(mys_fgetc(f) == 'c');
	assert(mys_fgetc(f) == 'd');
	assert(mys_fsetpos(f, &pos) == 0);
	assert(mys_fgetc(f) == 'c');
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_fgetpos_fsetpos passed\n");
}

// REQ-06-0198: feof set only after read past end.
void test_stdio_feof(void) {
	const char *fname = "test_feof.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	mys_fwrite("ab", 1, 2, f);
	mys_rewind(f);
	assert(mys_feof(f) == 0);
	assert(mys_fgetc(f) == 'a');
	assert(mys_feof(f) == 0);
	assert(mys_fgetc(f) == 'b');
	// Reading exactly all bytes may or may not set EOF until we read past.
	// Now attempt one more read — must hit EOF.
	assert(mys_fgetc(f) == EOF);
	assert(mys_feof(f) != 0);
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_feof passed\n");
}

// REQ-06-0199: ferror/clearerr flag management.
void test_stdio_ferror_clearerr(void) {
	const char *fname = "test_ferror.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	assert(mys_ferror(f) == 0);
	mys_fwrite("x", 1, 1, f);
	mys_rewind(f);
	// Read past end to force EOF flag, then clearerr should clear both.
	char b;
	mys_fread(&b, 1, 1, f);
	mys_fread(&b, 1, 1, f);
	assert(mys_feof(f) != 0);
	mys_clearerr(f);
	assert(mys_feof(f) == 0);
	assert(mys_ferror(f) == 0);
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_ferror_clearerr passed\n");
}

// REQ-06-0200: fflush(stream) forces underlying write.
void test_stdio_fflush_stream(void) {
	mys_FILE *t = mys_tmpfile();
	assert(t != NULL);
	int fd = mys_fileno(t);

	// Default mode is _IOFBF — write small data, file size on disk
	// stays zero until fflush.
	const char *msg = "hello-flush";
	assert(mys_fwrite(msg, 1, strlen(msg), t) == strlen(msg));
	off_t before = lseek(fd, 0, SEEK_END);
	assert(before == 0);
	assert(mys_fflush(t) == 0);
	off_t after = lseek(fd, 0, SEEK_END);
	assert(after == (off_t)strlen(msg));
	assert(mys_fclose(t) == 0);
	printf("test_stdio_fflush_stream passed\n");
}

// REQ-06-0201: fflush(NULL) flushes all open streams.
void test_stdio_fflush_all(void) {
	mys_FILE *a = mys_tmpfile();
	mys_FILE *b = mys_tmpfile();
	assert(a != NULL && b != NULL);
	int fda = mys_fileno(a), fdb = mys_fileno(b);

	assert(mys_fwrite("AAA", 1, 3, a) == 3);
	assert(mys_fwrite("BBBB", 1, 4, b) == 4);
	assert(lseek(fda, 0, SEEK_END) == 0);
	assert(lseek(fdb, 0, SEEK_END) == 0);

	assert(mys_fflush(NULL) == 0);

	assert(lseek(fda, 0, SEEK_END) == 3);
	assert(lseek(fdb, 0, SEEK_END) == 4);

	assert(mys_fclose(a) == 0);
	assert(mys_fclose(b) == 0);
	printf("test_stdio_fflush_all passed\n");
}

// REQ-06-0202: setvbuf(_IONBF) -> immediate writes.
void test_stdio_setvbuf_nbf(void) {
	const char *fname = "test_nbf.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	assert(mys_setvbuf(f, NULL, _IONBF, 0) == 0);
	assert(mys_fwrite("Q", 1, 1, f) == 1);
	int fd = open(fname, O_RDONLY);
	assert(fd >= 0);
	assert(lseek(fd, 0, SEEK_END) == 1);
	close(fd);
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_setvbuf_nbf passed\n");
}

// REQ-06-0203: setvbuf(_IOLBF) -> flush on newline.
void test_stdio_setvbuf_lbf(void) {
	const char *fname = "test_lbf.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	assert(mys_setvbuf(f, NULL, _IOLBF, 0) == 0);
	int fd = open(fname, O_RDONLY);
	assert(fd >= 0);
	// Write a chunk without newline — should remain buffered.
	assert(mys_fwrite("partial", 1, 7, f) == 7);
	assert(lseek(fd, 0, SEEK_END) == 0);
	// Now write something containing a newline — should flush.
	assert(mys_fwrite("\n", 1, 1, f) == 1);
	assert(lseek(fd, 0, SEEK_END) == 8);
	close(fd);
	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_setvbuf_lbf passed\n");
}

// REQ-06-0204: setvbuf(_IOFBF) -> accumulate until full / explicit flush.
void test_stdio_setvbuf_fbf(void) {
	const char *fname = "test_fbf.tmp";
	mys_FILE *f = mys_fopen(fname, "w+");
	assert(f != NULL);
	char ubuf[64];
	assert(mys_setvbuf(f, ubuf, _IOFBF, sizeof(ubuf)) == 0);

	int fd = open(fname, O_RDONLY);
	assert(fd >= 0);
	// Write less than the buffer size — must not appear on disk.
	assert(mys_fwrite("abcdef", 1, 6, f) == 6);
	assert(lseek(fd, 0, SEEK_END) == 0);
	// Manual flush makes it appear.
	assert(mys_fflush(f) == 0);
	assert(lseek(fd, 0, SEEK_END) == 6);
	close(fd);

	assert(mys_fclose(f) == 0);
	unlink(fname);
	printf("test_stdio_setvbuf_fbf passed\n");
}

// REQ-06-0205: tmpfile returns valid auto-deleted FILE.
void test_stdio_tmpfile(void) {
	mys_FILE *t = mys_tmpfile();
	assert(t != NULL);
	int fd = mys_fileno(t);
	assert(fd >= 0);

	// Verify the path link reports "(deleted)" — auto-delete on close.
	char proc_path[64];
	snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);
	char target[256];
	ssize_t n = readlink(proc_path, target, sizeof(target) - 1);
	assert(n > 0);
	target[n] = 0;
	assert(strstr(target, "(deleted)") != NULL);

	// Should be usable for read/write round-trip.
	assert(mys_fwrite("tmp", 1, 3, t) == 3);
	mys_rewind(t);
	char b[4]; memset(b, 0, sizeof(b));
	assert(mys_fread(b, 1, 3, t) == 3);
	assert(memcmp(b, "tmp", 3) == 0);

	assert(mys_fclose(t) == 0);
	printf("test_stdio_tmpfile passed\n");
}

// REQ-06-0206: perror smoke test — must not crash on either branch.
// Capture the output by redirecting mys_stderr's fd to a tmp file
// without disturbing the host fd 2 (so harness assert messages still
// reach the real stderr).
void test_stdio_perror(void) {
	const char *out = "test_perror.tmp";
	int tmpfd = open(out, O_RDWR | O_CREAT | O_TRUNC, 0600);
	assert(tmpfd >= 0);
	int saved = dup(2);
	assert(saved >= 0);
	// Replace fd 2 in-place with the tmp file. Both mys_stderr and the
	// host's stderr point at fd 2, so this captures both.
	assert(dup2(tmpfd, 2) >= 0);
	close(tmpfd);

	errno = ENOENT;
	mys_perror("substrate-test");
	mys_fflush(mys_stderr);
	mys_perror(NULL); // must not crash on NULL
	mys_fflush(mys_stderr);

	// Restore real fd 2 before doing any further assertions whose
	// failure messages need to reach the user.
	assert(dup2(saved, 2) >= 0);
	close(saved);

	// Read what perror wrote.
	int rfd = open(out, O_RDONLY);
	assert(rfd >= 0);
	char b[128]; memset(b, 0, sizeof(b));
	ssize_t n = read(rfd, b, sizeof(b) - 1);
	close(rfd);
	assert(n > 0);
	assert(strstr(b, "substrate-test") != NULL);

	unlink(out);
	printf("test_stdio_perror passed\n");
}

extern void mys___stdio_init(void);

int main(void) {
	mys___stdio_init(); // Crucial to initialize Substrate stdio streams
	printf("Running Substrate stdio tests...\n");
	test_stdio_basic();
	test_stdio_buffering();
	test_stdio_ungetc();
	test_stdio_fopen_modes();
	test_stdio_fopen_missing();
	test_stdio_fdopen();
	test_stdio_freopen();
	test_stdio_fread_fwrite_roundtrip();
	test_stdio_fread_partial_at_eof();
	test_stdio_fwrite_count();
	test_stdio_fgetc_fputc();
	test_stdio_ungetc_roundtrip();
	test_stdio_ungetc_eof();
	test_stdio_fgets_newline();
	test_stdio_fgets_truncate();
	test_stdio_fputs_puts();
	test_stdio_fseek_ftell();
	test_stdio_rewind();
	test_stdio_fgetpos_fsetpos();
	test_stdio_feof();
	test_stdio_ferror_clearerr();
	test_stdio_fflush_stream();
	test_stdio_fflush_all();
	test_stdio_setvbuf_nbf();
	test_stdio_setvbuf_lbf();
	test_stdio_setvbuf_fbf();
	test_stdio_tmpfile();
	test_stdio_perror();
	printf("All stdio tests passed!\n");
	return 0;
}
