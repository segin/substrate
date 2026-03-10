#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// These are our prefixed stdio functions
typedef struct mys_FILE mys_FILE;
extern mys_FILE *mys_fopen(const char *path, const char *mode);
extern int mys_fclose(mys_FILE *fp);
extern size_t mys_fread(void *ptr, size_t size, size_t nmemb, mys_FILE *stream);
extern size_t mys_fwrite(const void *ptr, size_t size, size_t nmemb, mys_FILE *stream);
extern int mys_fseek(mys_FILE *stream, long offset, int whence);
extern long mys_ftell(mys_FILE *stream);
extern void mys_rewind(mys_FILE *stream);
extern int mys_fgetc(mys_FILE *stream);
extern int mys_fputc(int c, mys_FILE *stream);
extern int mys_ungetc(int c, mys_FILE *stream);
extern char *mys_fgets(char *s, int size, mys_FILE *stream);
extern int mys_fputs(const char *s, mys_FILE *stream);
extern int mys_fflush(mys_FILE *stream);
extern int mys_feof(mys_FILE *stream);
extern int mys_ferror(mys_FILE *stream);
extern void mys_clearerr(mys_FILE *stream);
extern int mys_setvbuf(mys_FILE *stream, char *buf, int mode, size_t size);
extern mys_FILE *mys_tmpfile(void);

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

extern void mys___stdio_init(void);

int main(void) {
	mys___stdio_init(); // Crucial to initialize Substrate stdio streams
	printf("Running Substrate stdio tests...\n");
	test_stdio_basic();
	test_stdio_buffering();
	test_stdio_ungetc();
	printf("All stdio tests passed!\n");
	return 0;
}
