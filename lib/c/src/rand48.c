/*
 * rand48.c — the SVID 48-bit linear-congruential PRNG, reentrant variants.
 *
 * State is a 48-bit value X advanced by  X = (a*X + c) mod 2^48,  with the
 * SVID defaults a = 0x5DEECE66D, c = 0xB.  The reentrant *_r forms keep that
 * state in a caller-supplied `struct drand48_data` (or, for the e/n/j forms,
 * in a caller-supplied unsigned short[3]) so concurrent users never share the
 * hidden global the plain drand48()/lrand48()/mrand48() family uses.
 */

#include <stdlib.h>
#include <string.h>

#define RAND48_MULT_DEFAULT 0x5DEECE66DULL
#define RAND48_ADD_DEFAULT  0xB
#define RAND48_MASK         0xFFFFFFFFFFFFULL   /* low 48 bits */

/* Advance the three-short state in place using the multiplier/addend from the
 * buffer, and return the new 48-bit value. */
static unsigned long long
rand48_step(unsigned short xsubi[3], const struct drand48_data *d)
{
    unsigned long long x =
        (unsigned long long)xsubi[2] << 32 |
        (unsigned long long)xsubi[1] << 16 |
        (unsigned long long)xsubi[0];
    x = (d->__a * x + d->__c) & RAND48_MASK;
    xsubi[0] = (unsigned short)(x & 0xFFFF);
    xsubi[1] = (unsigned short)((x >> 16) & 0xFFFF);
    xsubi[2] = (unsigned short)((x >> 32) & 0xFFFF);
    return x;
}

static void
rand48_defaults(struct drand48_data *d)
{
    if (!d->__init) {
        d->__a = RAND48_MULT_DEFAULT;
        d->__c = RAND48_ADD_DEFAULT;
        d->__init = 1;
    }
}

int
erand48_r(unsigned short xsubi[3], struct drand48_data *buffer, double *result)
{
    unsigned long long x;
    rand48_defaults(buffer);
    x = rand48_step(xsubi, buffer);
    /* 48-bit value scaled into [0, 1): x / 2^48. */
    *result = (double)x / 281474976710656.0;
    return 0;
}

int
drand48_r(struct drand48_data *buffer, double *result)
{
    rand48_defaults(buffer);
    return erand48_r(buffer->__x, buffer, result);
}

int
nrand48_r(unsigned short xsubi[3], struct drand48_data *buffer, long *result)
{
    unsigned long long x;
    rand48_defaults(buffer);
    x = rand48_step(xsubi, buffer);
    *result = (long)(x >> 17);              /* top 31 bits, non-negative */
    return 0;
}

int
lrand48_r(struct drand48_data *buffer, long *result)
{
    rand48_defaults(buffer);
    return nrand48_r(buffer->__x, buffer, result);
}

int
jrand48_r(unsigned short xsubi[3], struct drand48_data *buffer, long *result)
{
    unsigned long long x;
    rand48_defaults(buffer);
    x = rand48_step(xsubi, buffer);
    *result = (long)(int)(unsigned)(x >> 16);   /* top 32 bits, signed */
    return 0;
}

int
mrand48_r(struct drand48_data *buffer, long *result)
{
    rand48_defaults(buffer);
    return jrand48_r(buffer->__x, buffer, result);
}

int
srand48_r(long seedval, struct drand48_data *buffer)
{
    buffer->__x[0] = 0x330E;
    buffer->__x[1] = (unsigned short)(seedval & 0xFFFF);
    buffer->__x[2] = (unsigned short)((seedval >> 16) & 0xFFFF);
    buffer->__a = RAND48_MULT_DEFAULT;
    buffer->__c = RAND48_ADD_DEFAULT;
    buffer->__init = 1;
    return 0;
}

int
seed48_r(unsigned short seed16v[3], struct drand48_data *buffer)
{
    /* Stash the old internal state so the caller can restore it later. */
    memcpy(buffer->__old_x, buffer->__x, sizeof buffer->__old_x);
    buffer->__x[0] = seed16v[0];
    buffer->__x[1] = seed16v[1];
    buffer->__x[2] = seed16v[2];
    buffer->__a = RAND48_MULT_DEFAULT;
    buffer->__c = RAND48_ADD_DEFAULT;
    buffer->__init = 1;
    return 0;
}

int
lcong48_r(unsigned short param[7], struct drand48_data *buffer)
{
    buffer->__x[0] = param[0];
    buffer->__x[1] = param[1];
    buffer->__x[2] = param[2];
    buffer->__a = (unsigned long long)param[3] |
                  (unsigned long long)param[4] << 16 |
                  (unsigned long long)param[5] << 32;
    buffer->__c = param[6];
    buffer->__init = 1;
    return 0;
}

/*
 * Non-reentrant family: the classic drand48()/lrand48()/... interface, all
 * sharing a single hidden global state object (lazily SVID-default-seeded by
 * the _r forms).  Not thread-safe — concurrent users want the _r forms above.
 */
static struct drand48_data __drand48_global;

double drand48(void)
{
    double r;
    drand48_r(&__drand48_global, &r);
    return r;
}

double erand48(unsigned short xsubi[3])
{
    double r;
    erand48_r(xsubi, &__drand48_global, &r);
    return r;
}

long lrand48(void)
{
    long r;
    lrand48_r(&__drand48_global, &r);
    return r;
}

long nrand48(unsigned short xsubi[3])
{
    long r;
    nrand48_r(xsubi, &__drand48_global, &r);
    return r;
}

long mrand48(void)
{
    long r;
    mrand48_r(&__drand48_global, &r);
    return r;
}

long jrand48(unsigned short xsubi[3])
{
    long r;
    jrand48_r(xsubi, &__drand48_global, &r);
    return r;
}

void srand48(long seedval)
{
    srand48_r(seedval, &__drand48_global);
}

unsigned short *seed48(unsigned short seed16v[3])
{
    seed48_r(seed16v, &__drand48_global);
    return __drand48_global.__old_x;
}

void lcong48(unsigned short param[7])
{
    lcong48_r(param, &__drand48_global);
}
