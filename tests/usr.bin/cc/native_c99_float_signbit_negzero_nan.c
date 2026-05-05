#include <math.h>

float g_mzf = -0.0f;
double g_mzd = -0.0;
long double g_mzl = -0.0L;

static long double local_mzl(void) {
    long double x = -0.0L;
    return x;
}

static float pos_nanf(void) {
    volatile float nan = 0.0f / 0.0f;
    return signbit(nan) ? -nan : nan;
}

static float neg_nanf(void) {
    volatile float nan = 0.0f / 0.0f;
    return signbit(nan) ? nan : -nan;
}

int main(void) {
    long double l = local_mzl();
    if (!signbit(g_mzf) || !signbit(g_mzd) || !signbit(g_mzl) || !signbit(l)) return 1;
    if (signbit(pos_nanf())) return 2;
    if (!signbit(neg_nanf())) return 3;
    return 0;
}
