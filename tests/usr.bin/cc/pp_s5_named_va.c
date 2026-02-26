#define LOGF(fmt, args...) fmt , ## args

int a0 = LOGF(7);
int a1 = LOGF(9, 4);
