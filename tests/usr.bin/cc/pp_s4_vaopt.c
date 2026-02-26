#define M(x, ...) x __VA_OPT__(+ (__VA_ARGS__))
int v = M(1, 2);
