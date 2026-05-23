/*
 * <values.h> — legacy SVID compat header.
 *
 * Deprecated but still referenced by ported software (libxshmfence,
 * historical SunOS / SysV code).  The few constants it defines all
 * have direct equivalents in <limits.h> / <float.h>; redirect there.
 */
#ifndef _VALUES_H
#define _VALUES_H

#include <limits.h>
#include <float.h>

#ifndef MAXINT
#define MAXINT      INT_MAX
#endif
#ifndef MAXLONG
#define MAXLONG     LONG_MAX
#endif
#ifndef MAXSHORT
#define MAXSHORT    SHRT_MAX
#endif
#ifndef MAXCHAR
#define MAXCHAR     CHAR_MAX
#endif
#ifndef MAXDOUBLE
#define MAXDOUBLE   DBL_MAX
#endif
#ifndef MAXFLOAT
#define MAXFLOAT    FLT_MAX
#endif
#ifndef MINDOUBLE
#define MINDOUBLE   DBL_MIN
#endif
#ifndef MINFLOAT
#define MINFLOAT    FLT_MIN
#endif
#ifndef BITSPERBYTE
#define BITSPERBYTE CHAR_BIT
#endif

#endif /* _VALUES_H */
