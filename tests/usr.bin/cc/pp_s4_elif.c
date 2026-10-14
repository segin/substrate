#define FLAG 1

#if 0
int a = 0;
#elifdef FLAG
int a = 1;
#else
int a = 2;
#endif

#if 0
int b = 0;
#elifndef MISSING
int b = 3;
#else
int b = 4;
#endif
