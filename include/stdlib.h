#ifndef _STDLIB_H
#define _STDLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* wchar_t comes from stddef.h above. */

#define RAND_MAX 2147483647

/* MB_CUR_MAX must reflect the current locale's maximum multibyte length.
 * substrate's libc is a single-byte "C" locale (mbtowc/mblen are single-byte;
 * __ctype_get_mb_cur_max() returns 1).  Hardcoding 4 lied about multibyte
 * support the libc does not actually provide: wide-character-aware clients
 * keyed on MB_CUR_MAX (notably CDE's dtterm, which then stores cells as
 * wchar_t and draws via XwcDrawString) rendered every ASCII cell as one glyph
 * followed by three NUL "tofu" boxes.  Use the accessor so MB_CUR_MAX tracks
 * the real locale (and would adapt automatically if multibyte support lands). */
extern int __ctype_get_mb_cur_max(void);
#define MB_CUR_MAX (__ctype_get_mb_cur_max())

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

typedef struct {
    long long quot;
    long long rem;
} lldiv_t;

[[noreturn]] void exit(int status);
[[noreturn]] void _Exit(int status);
[[noreturn]] void abort(void);

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void *aligned_alloc(size_t alignment, size_t size);
int posix_memalign(void **memptr, size_t alignment, size_t size);

int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);
double atof(const char *nptr);
double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);

char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *string);
char *realpath(const char *__restrict path, char *__restrict resolved_path);
int system(const char *command);

void quick_exit(int status);
int at_quick_exit(void (*func)(void));
int atexit(void (*func)(void));

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void qsort_r(void *base, size_t nmemb, size_t size,
             int (*compar)(const void *, const void *, void *),
             void *arg);
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

/* C89 stateless multibyte conversion.  See <wchar.h> for restartable
 * forms (mbrtowc/wcrtomb). */
int mblen(const char *s, size_t n);
int mbtowc(wchar_t *pwc, const char *s, size_t n);
int wctomb(char *s, wchar_t wc);
size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n);
size_t wcstombs(char *s, const wchar_t *pwcs, size_t n);

int abs(int j);
long labs(long j);
long long llabs(long long j);

div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);
lldiv_t lldiv(long long numer, long long denom);

int rand(void);
int rand_r(unsigned int *seedp);
void srand(unsigned int seed);
long random(void);
void srandom(unsigned seed);

/* Reentrant random(3): generator state held in a caller-supplied object and
 * state array (see initstate_r(3)). */
struct random_data {
    int32_t *fptr;        /* front pointer into the state ring */
    int32_t *rptr;        /* rear pointer into the state ring */
    int32_t *state;       /* the state array (word [-1] holds packed type) */
    int      rand_type;   /* TYPE_0..TYPE_4 */
    int      rand_deg;    /* degree of the feedback polynomial */
    int      rand_sep;    /* separation between fptr and rptr */
    int32_t *end_ptr;     /* one past the end of the state array */
};
int random_r(struct random_data *buf, int32_t *result);
int srandom_r(unsigned int seed, struct random_data *buf);
int initstate_r(unsigned int seed, char *statebuf, size_t statelen,
                struct random_data *buf);
int setstate_r(char *statebuf, struct random_data *buf);

/* Reentrant SVID 48-bit PRNG: state held in a caller-supplied object. */
struct drand48_data {
    unsigned short __x[3];       /* current 48-bit state */
    unsigned short __old_x[3];   /* state saved by seed48_r() */
    unsigned short __c;          /* additive constant */
    unsigned short __init;       /* nonzero once seeded */
    unsigned long long __a;      /* 48-bit multiplier */
};
int drand48_r(struct drand48_data *buffer, double *result);
int erand48_r(unsigned short xsubi[3], struct drand48_data *buffer, double *result);
int lrand48_r(struct drand48_data *buffer, long *result);
int nrand48_r(unsigned short xsubi[3], struct drand48_data *buffer, long *result);
int mrand48_r(struct drand48_data *buffer, long *result);
int jrand48_r(unsigned short xsubi[3], struct drand48_data *buffer, long *result);
int srand48_r(long seedval, struct drand48_data *buffer);
int seed48_r(unsigned short seed16v[3], struct drand48_data *buffer);
int lcong48_r(unsigned short param[7], struct drand48_data *buffer);

/* Non-reentrant SVID 48-bit PRNG: state held in a single hidden global. */
double         drand48(void);
double         erand48(unsigned short xsubi[3]);
long           lrand48(void);
long           nrand48(unsigned short xsubi[3]);
long           mrand48(void);
long           jrand48(unsigned short xsubi[3]);
void           srand48(long seedval);
unsigned short *seed48(unsigned short seed16v[3]);
void           lcong48(unsigned short param[7]);

/* Reentrant ecvt(3)/fcvt(3): write the converted digits into a caller buffer. */
int ecvt_r(double value, int ndigit, int *decpt, int *sign, char *buf, size_t len);
int fcvt_r(double value, int ndigit, int *decpt, int *sign, char *buf, size_t len);
int qecvt_r(long double value, int ndigit, int *decpt, int *sign, char *buf, size_t len);
int qfcvt_r(long double value, int ndigit, int *decpt, int *sign, char *buf, size_t len);

uint32_t arc4random(void);
void arc4random_buf(void *buf, size_t n);
uint32_t arc4random_uniform(uint32_t upper_bound);
int mkstemp(char *tmpl);
int mkstemps(char *tmpl, int suffixlen);
char *mkdtemp(char *tmpl);
void *reallocarray(void *ptr, size_t nmemb, size_t size);

/* Unix98 pseudo-terminal helpers (XSI). */
int posix_openpt(int flags);
int grantpt(int fd);
int unlockpt(int fd);
char *ptsname(int fd);
int ptsname_r(int fd, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif
#endif
