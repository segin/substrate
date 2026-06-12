/*
 * math_rem_pio2.h - private interface to the Payne-Hanek argument
 * reduction used by math_trig.c.
 *
 * __ieee754_rem_pio2(x, y) reduces x mod pi/2:
 *   - y[0]+y[1] is the reduced argument r in [-pi/4, pi/4]
 *     (double-double; y[0] alone is full-precision for our use).
 *   - the return value n gives the quadrant: the caller uses (n & 3)
 *     to select sin/cos from fsin(r)/fcos(r).
 */

#ifndef MATH_REM_PIO2_H
#define MATH_REM_PIO2_H

int __ieee754_rem_pio2(double x, double *y);

#endif /* MATH_REM_PIO2_H */
