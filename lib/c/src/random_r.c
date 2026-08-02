/*
 * random_r.c — reentrant BSD random(3) generator.
 *
 * A faithful port of the classic 4.3BSD additive-feedback random number
 * generator in its reentrant form: all state lives in a caller-supplied
 * `struct random_data` plus a caller-supplied state array, so concurrent
 * streams never collide on a hidden global.  Five generator qualities are
 * selected by the state-array size, exactly as initstate(3) documents:
 *
 *   state bytes  type     degree  separation
 *      8..31     TYPE_0     0        0      (pure linear congruential)
 *     32..63     TYPE_1     7        3
 *     64..127    TYPE_2    15        1
 *    128..255    TYPE_3    31        3      (the srandom() default)
 *    >=256       TYPE_4    63        1
 *
 * The first int32 of the state array holds packed type + rear-pointer info so
 * setstate_r() can resume a saved array.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#define TYPE_0   0
#define BREAK_0  8
#define DEG_0    0
#define SEP_0    0
#define TYPE_1   1
#define BREAK_1  32
#define TYPE_2   2
#define BREAK_2  64
#define TYPE_3   3
#define BREAK_3  128
#define TYPE_4   4
#define BREAK_4  256
#define MAX_TYPES 5

static const int random_degrees[MAX_TYPES] = { 0, 7, 15, 31, 63 };
static const int random_seps[MAX_TYPES]    = { 0, 3, 1, 3, 1 };

int
random_r(struct random_data *buf, int32_t *result)
{
    int32_t *state;

    if (buf == NULL || result == NULL)
        goto fail;
    state = buf->state;

    if (buf->rand_type == TYPE_0) {
        int32_t val = (int32_t)(((uint32_t)state[0] * 1103515245u + 12345u)
                                & 0x7fffffffu);
        state[0] = val;
        *result = val;
    } else {
        int32_t *fptr    = buf->fptr;
        int32_t *rptr    = buf->rptr;
        int32_t *end_ptr = buf->end_ptr;
        uint32_t val;

        val = (uint32_t)*fptr + (uint32_t)*rptr;
        *fptr = (int32_t)val;
        /* Chucking the least-significant bit improves the distribution. */
        *result = (int32_t)((val >> 1) & 0x7fffffffu);

        if (++fptr >= end_ptr) {
            fptr = state;
            ++rptr;
        } else if (++rptr >= end_ptr) {
            rptr = state;
        }
        buf->fptr = fptr;
        buf->rptr = rptr;
    }
    return 0;

fail:
    errno = EINVAL;
    return -1;
}

int
srandom_r(unsigned int seed, struct random_data *buf)
{
    int      type, kc, i;
    int32_t *state, *dst;
    int32_t  word, discard;

    if (buf == NULL)
        goto fail;
    type = buf->rand_type;
    if ((unsigned int)type >= MAX_TYPES)
        goto fail;
    state = buf->state;

    /* A seed of 0 would wedge the linear-congruential warm-up at 0. */
    if (seed == 0)
        seed = 1;
    state[0] = (int32_t)seed;
    if (type == TYPE_0)
        return 0;

    /* Fill the state with a Park-Miller minimal-standard LCG. */
    dst  = state;
    word = (int32_t)seed;
    kc   = buf->rand_deg;
    for (i = 1; i < kc; i++) {
        int32_t hi = word / 127773;
        int32_t lo = word % 127773;
        word = 16807 * lo - 2836 * hi;
        if (word < 0)
            word += 2147483647;
        *++dst = word;
    }
    buf->fptr = &state[buf->rand_sep];
    buf->rptr = &state[0];
    /* Cycle the generator to shake out the initial regularity. */
    kc *= 10;
    while (--kc >= 0)
        (void)random_r(buf, &discard);
    return 0;

fail:
    errno = EINVAL;
    return -1;
}

int
initstate_r(unsigned int seed, char *arg_state, size_t n, struct random_data *buf)
{
    int32_t *old_state, *state;
    int      type, degree, separation;

    if (buf == NULL || arg_state == NULL)
        goto fail;

    /* Record the type back into any state array we are leaving. */
    old_state = buf->state;
    if (old_state != NULL) {
        int old_type = buf->rand_type;
        if (old_type == TYPE_0)
            old_state[-1] = TYPE_0;
        else
            old_state[-1] = (int32_t)(MAX_TYPES * (buf->rptr - old_state)
                                      + old_type);
    }

    if (n >= BREAK_3)
        type = (n < BREAK_4) ? TYPE_3 : TYPE_4;
    else if (n < BREAK_1) {
        if (n < BREAK_0)
            goto fail;                 /* too small to be useful */
        type = TYPE_0;
    } else
        type = (n < BREAK_2) ? TYPE_1 : TYPE_2;

    degree     = random_degrees[type];
    separation = random_seps[type];

    buf->rand_type = type;
    buf->rand_deg  = degree;
    buf->rand_sep  = separation;

    /* The first word is reserved for the packed type/rear info. */
    state = &((int32_t *)arg_state)[1];
    buf->state = state;
    /* end_ptr must be valid BEFORE srandom_r(): its warm-up loop calls
     * random_r(), which wraps fptr/rptr against end_ptr. */
    buf->end_ptr = &state[degree];

    srandom_r(seed, buf);

    if (type == TYPE_0)
        state[-1] = TYPE_0;
    else
        state[-1] = (int32_t)(MAX_TYPES * (buf->rptr - state) + type);
    return 0;

fail:
    errno = EINVAL;
    return -1;
}

int
setstate_r(char *arg_state, struct random_data *buf)
{
    int32_t *new_state;
    int      type, rear;

    if (arg_state == NULL || buf == NULL)
        goto fail;
    new_state = &((int32_t *)arg_state)[1];

    /* Save the type info into the array we are leaving. */
    {
        int32_t *old_state = buf->state;
        int      old_type  = buf->rand_type;
        if (old_type == TYPE_0)
            old_state[-1] = TYPE_0;
        else
            old_state[-1] = (int32_t)(MAX_TYPES * (buf->rptr - old_state)
                                      + old_type);
    }

    type = new_state[-1] % MAX_TYPES;
    if (type < TYPE_0 || type > TYPE_4)
        goto fail;

    buf->rand_deg = random_degrees[type];
    buf->rand_sep = random_seps[type];
    buf->rand_type = type;

    if (type != TYPE_0) {
        rear = new_state[-1] / MAX_TYPES;
        buf->rptr = &new_state[rear];
        buf->fptr = &new_state[(rear + buf->rand_sep) % buf->rand_deg];
    }
    buf->state = new_state;
    buf->end_ptr = &new_state[buf->rand_deg];
    return 0;

fail:
    errno = EINVAL;
    return -1;
}
