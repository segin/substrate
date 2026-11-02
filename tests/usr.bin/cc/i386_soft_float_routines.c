/*
 * libgcc's i386 soft-float routines, by their contracts, done with the x87,
 * for the tests of -mno-80387 code (run_i386_soft_float*.sh): i386 libgcc
 * ships none of them, as it has the x87.  Compiled by gcc with the x87 and
 * without SSE, each takes and returns the bits as integers -- a float's in
 * %eax, a double's in %edx:%eax -- and for a long double result a thunk
 * returns its 12 bytes in %eax:%edx:%ecx.
 */
#include <string.h>

typedef struct { unsigned w[3]; } xf;
typedef unsigned sf;
typedef unsigned long long df;

static float F(sf a) { float x; memcpy(&x, &a, 4); return x; }
static double D(df a) { double x; memcpy(&x, &a, 8); return x; }
static long double X(xf a) { long double x = 0; memcpy(&x, &a, 10); return x; }
static sf f_(float x) { sf a; memcpy(&a, &x, 4); return a; }
static df d_(double x) { df a; memcpy(&a, &x, 8); return a; }
static void x_(xf *r, long double x) { memset(r, 0, sizeof(*r)); memcpy(r, &x, 10); }

#define ARITH(op, o) \
	sf __##op##sf3(sf a, sf b) { return f_(F(a) o F(b)); } \
	df __##op##df3(df a, df b) { return d_(D(a) o D(b)); } \
	void xf_##op(xf *r, const xf *a) { x_(r, X(a[0]) o X(a[1])); }
ARITH(add, +)
ARITH(sub, -)
ARITH(mul, *)
ARITH(div, /)

/* -1, 0 or 1 as a < b, a == b, a > b; unordered: nonzero for eq and ne, 1 for lt and le, -1 for gt and ge */
#define CMPS(name, U) \
	CMPN(name, sf, F, U) CMPN(name, df, D, U) CMPN(name, xf, X, U)
#define CMPN(name, T, C, U) int __##name##T##2(T a, T b) { return C(a) < C(b) ? -1 : C(a) == C(b) ? 0 : C(a) > C(b) ? 1 : (U); }
CMPS(eq, 1) CMPS(ne, 1) CMPS(lt, 1) CMPS(le, 1) CMPS(gt, -1) CMPS(ge, -1)
int __unordsf2(sf a, sf b) { return F(a) != F(a) || F(b) != F(b); }
int __unorddf2(df a, df b) { return D(a) != D(a) || D(b) != D(b); }
int __unordxf2(xf a, xf b) { return X(a) != X(a) || X(b) != X(b); }

#define FROMINT(name, T) \
	sf __float##name##sf(T i) { return f_((float)i); } \
	df __float##name##df(T i) { return d_((double)i); } \
	void xf_float##name(xf *r, const T *i) { x_(r, (long double)*i); }
FROMINT(si, int)
FROMINT(unsi, unsigned)
FROMINT(di, long long)
FROMINT(undi, unsigned long long)

int __fixsfsi(sf a) { return (int)F(a); }
int __fixdfsi(df a) { return (int)D(a); }
int __fixxfsi(xf a) { return (int)X(a); }
unsigned __fixunssfsi(sf a) { return (unsigned)F(a); }
unsigned __fixunsdfsi(df a) { return (unsigned)D(a); }
unsigned __fixunsxfsi(xf a) { return (unsigned)X(a); }
long long __fixsfdi(sf a) { return (long long)F(a); }
long long __fixdfdi(df a) { return (long long)D(a); }
long long __fixxfdi(xf a) { return (long long)X(a); }
unsigned long long __fixunssfdi(sf a) { return (unsigned long long)F(a); }
unsigned long long __fixunsdfdi(df a) { return (unsigned long long)D(a); }
unsigned long long __fixunsxfdi(xf a) { return (unsigned long long)X(a); }

df __extendsfdf2(sf a) { return d_((double)F(a)); }
sf __truncdfsf2(df a) { return f_((float)D(a)); }
sf __truncxfsf2(xf a) { return f_((float)X(a)); }
df __truncxfdf2(xf a) { return d_((double)X(a)); }
void xf_extendsf(xf *r, const sf *a) { x_(r, (long double)F(*a)); }
void xf_extenddf(xf *r, const df *a) { x_(r, (long double)D(*a)); }

/*
 * A long double result in %eax:%edx:%ecx: the thunk calls the C routine with the result's buffer and the
 * address of its own arguments, then returns the buffer's three words.
 */
#define XF(name, impl) \
	__asm__(".text\n.globl " #name "\n.type " #name ", @function\n" #name ":\n" \
		"\tsubl $28, %esp\n\tleal 32(%esp), %eax\n\tleal 12(%esp), %ecx\n" \
		"\tmovl %ecx, 0(%esp)\n\tmovl %eax, 4(%esp)\n\tcall " #impl "\n" \
		"\tmovl 12(%esp), %eax\n\tmovl 16(%esp), %edx\n\tmovl 20(%esp), %ecx\n" \
		"\taddl $28, %esp\n\tret\n");
XF(__addxf3, xf_add)
XF(__subxf3, xf_sub)
XF(__mulxf3, xf_mul)
XF(__divxf3, xf_div)
XF(__floatsixf, xf_floatsi)
XF(__floatunsixf, xf_floatunsi)
XF(__floatdixf, xf_floatdi)
XF(__floatundixf, xf_floatundi)
XF(__extendsfxf2, xf_extendsf)
XF(__extenddfxf2, xf_extenddf)
