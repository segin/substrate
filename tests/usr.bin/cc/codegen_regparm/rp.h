/* rp.h -- the declarations both halves of the regparm ABI test share. */
#ifndef RP_H
#define RP_H

struct s2 { short a; };
struct s4 { int x; };
struct s6 { short a, b, c; };
struct s8 { int x, y; };
struct s12 { int x, y, z; };
struct s16 { int a, b, c, d; };
struct sd { double d; };

int rp_ints(int a, int b, int c);
int rp_four(int a, int b, int c, int d);
long long rp_ll_first(long long x, int y, int z);
long long rp_ll_late(int a, int b, long long x, int d);
int rp_narrow(char c, short s, unsigned char u, int i);
int rp_s2(struct s2 s, int d);
int rp_s4(struct s4 s, int d, int e);
int rp_s6(struct s6 s, int d);
int rp_s8(int a, struct s8 s, int d);
int rp_s12(struct s12 s, int d, int e);
int rp_s16(int a, struct s16 s, int d);
int rp_fp(float f, int a, double d, int b, struct sd s, int c);
int rp_var(int n, ...);
int __attribute__((regparm(0))) rp_attr0(int a, int b, int c);
int __attribute__((regparm(2))) rp_attr2(int a, int b, int c);
int rp_call_ptr(int (*fn)(int, int, int), int a);
struct s12 rp_ret(int a, int b);

/* returned structures, for -freg-struct-return: in registers at 1, 2, 4 and 8 bytes, in %st0 as one float or double */
struct r1 { char a; };
struct r3 { char a[3]; };
struct r5 { char a[5]; };
struct sf { float f; };
struct sff { float a, b; };
struct r1 rp_r1(int a);
struct s2 rp_r2(int a);
struct r3 rp_r3(int a);
struct s4 rp_r4(int a);
struct r5 rp_r5(int a);
struct s8 rp_r8(int a, int b);
struct sf rp_rf(int a);
struct sd rp_rd(int a);
struct sff rp_rff(int a);

#endif
