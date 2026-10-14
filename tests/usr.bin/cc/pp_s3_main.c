#include "pp_s3_quote.h"
#include <pp_s3_system.h>

#define OBJ 10
#define SUM(a, b) ((a) + (b))
#define VADD(x, y) ((x) + (y))
#define STR(x) #x
#define CAT(a, b) a##b
#define TMP 9
#undef TMP

int CAT(v, ar) = OBJ;
const char *s = STR(hello);
int a = SUM(1, 2);
int b = VADD(3, 4);

#if defined(OBJ) && (0 && (1 / 0))
#error short-circuit-broken
#endif

#line 77 "virt.c"
int line_marker_probe = __LINE__;
int imports = QQ + SS + FORCE + MACDEF;
