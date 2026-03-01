#ifndef _SUBSTRATE_EMMINTRIN_H_
#define _SUBSTRATE_EMMINTRIN_H_

#include <stdint.h>
#include <xmmintrin.h>

typedef struct __attribute__((aligned(16))) {
	int64_t q[2];
} __m128i;

typedef struct __attribute__((aligned(16))) {
	double d[2];
} __m128d;

typedef union {
	__m128i v;
	uint8_t u8[16];
	int8_t i8[16];
	uint16_t u16[8];
	int16_t i16[8];
	uint32_t u32[4];
	int32_t i32[4];
	uint64_t u64[2];
	int64_t i64[2];
} __m128i_u;

typedef union {
	__m128d v;
	double d[2];
	uint64_t u64[2];
} __m128d_u;

static __inline__ __m128i _mm_setzero_si128(void) {
	__m128i_u r;
	int i;
	for(i=0;i<2;i++) r.u64[i] = 0u;
	return(r.v);
}

static __inline__ __m128d _mm_setzero_pd(void) {
	__m128d_u r;
	int i;
	for(i=0;i<2;i++) r.u64[i] = 0u;
	return(r.v);
}

static __inline__ __m128d _mm_set1_pd(double x) {
	__m128d_u r;
	r.d[0] = x;
	r.d[1] = x;
	return(r.v);
}

static __inline__ __m128i _mm_set1_epi8(char x) {
	__m128i_u r;
	int i;
	for(i=0;i<16;i++) r.i8[i] = x;
	return(r.v);
}

static __inline__ __m128i _mm_set1_epi16(short x) {
	__m128i_u r;
	int i;
	for(i=0;i<8;i++) r.i16[i] = x;
	return(r.v);
}

static __inline__ __m128i _mm_set1_epi32(int x) {
	__m128i_u r;
	int i;
	for(i=0;i<4;i++) r.i32[i] = x;
	return(r.v);
}

static __inline__ __m128i _mm_set1_epi64x(long long x) {
	__m128i_u r;
	r.i64[0] = x;
	r.i64[1] = x;
	return(r.v);
}

static __inline__ __m128i _mm_set_epi32(int e3, int e2, int e1, int e0) {
	__m128i_u r;
	r.i32[0] = e0;
	r.i32[1] = e1;
	r.i32[2] = e2;
	r.i32[3] = e3;
	return(r.v);
}

static __inline__ __m128i _mm_setr_epi32(int e0, int e1, int e2, int e3) {
	return(_mm_set_epi32(e3, e2, e1, e0));
}

static __inline__ __m128i _mm_load_si128(const __m128i *p) {
	return(*p);
}

static __inline__ __m128i _mm_loadu_si128(const __m128i *p) {
	__m128i_u r, up;
	up.v = *p;
	r = up;
	return(r.v);
}

static __inline__ void _mm_store_si128(__m128i *p, __m128i a) {
	*p = a;
}

static __inline__ void _mm_storeu_si128(__m128i *p, __m128i a) {
	*p = a;
}

static __inline__ __m128d _mm_load_pd(const double *p) {
	__m128d_u r;
	r.d[0] = p[0];
	r.d[1] = p[1];
	return(r.v);
}

static __inline__ __m128d _mm_loadu_pd(const double *p) {
	return(_mm_load_pd(p));
}

static __inline__ void _mm_store_pd(double *p, __m128d a) {
	__m128d_u ua;
	ua.v = a;
	p[0] = ua.d[0];
	p[1] = ua.d[1];
}

static __inline__ void _mm_storeu_pd(double *p, __m128d a) {
	_mm_store_pd(p, a);
}

static __inline__ __m128i _mm_add_epi8(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<16;i++) r.i8[i] = (int8_t)(ua.i8[i] + ub.i8[i]);
	return(r.v);
}

static __inline__ __m128i _mm_add_epi16(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<8;i++) r.i16[i] = (int16_t)(ua.i16[i] + ub.i16[i]);
	return(r.v);
}

static __inline__ __m128i _mm_add_epi32(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<4;i++) r.i32[i] = ua.i32[i] + ub.i32[i];
	return(r.v);
}

static __inline__ __m128i _mm_add_epi64(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.i64[0] = ua.i64[0] + ub.i64[0];
	r.i64[1] = ua.i64[1] + ub.i64[1];
	return(r.v);
}

static __inline__ __m128i _mm_sub_epi8(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<16;i++) r.i8[i] = (int8_t)(ua.i8[i] - ub.i8[i]);
	return(r.v);
}

static __inline__ __m128i _mm_sub_epi16(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<8;i++) r.i16[i] = (int16_t)(ua.i16[i] - ub.i16[i]);
	return(r.v);
}

static __inline__ __m128i _mm_sub_epi32(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<4;i++) r.i32[i] = ua.i32[i] - ub.i32[i];
	return(r.v);
}

static __inline__ __m128i _mm_sub_epi64(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.i64[0] = ua.i64[0] - ub.i64[0];
	r.i64[1] = ua.i64[1] - ub.i64[1];
	return(r.v);
}

static __inline__ __m128i _mm_and_si128(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.u64[0] = ua.u64[0] & ub.u64[0];
	r.u64[1] = ua.u64[1] & ub.u64[1];
	return(r.v);
}

static __inline__ __m128i _mm_or_si128(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.u64[0] = ua.u64[0] | ub.u64[0];
	r.u64[1] = ua.u64[1] | ub.u64[1];
	return(r.v);
}

static __inline__ __m128i _mm_xor_si128(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.u64[0] = ua.u64[0] ^ ub.u64[0];
	r.u64[1] = ua.u64[1] ^ ub.u64[1];
	return(r.v);
}

static __inline__ __m128i _mm_cmpeq_epi8(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<16;i++) r.u8[i] = (ua.i8[i] == ub.i8[i]) ? 0xffu : 0u;
	return(r.v);
}

static __inline__ __m128i _mm_cmpeq_epi16(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<8;i++) r.u16[i] = (ua.i16[i] == ub.i16[i]) ? 0xffffu : 0u;
	return(r.v);
}

static __inline__ __m128i _mm_cmpeq_epi32(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<4;i++) r.u32[i] = (ua.i32[i] == ub.i32[i]) ? 0xffffffffu : 0u;
	return(r.v);
}

static __inline__ __m128i _mm_cmpgt_epi8(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<16;i++) r.u8[i] = (ua.i8[i] > ub.i8[i]) ? 0xffu : 0u;
	return(r.v);
}

static __inline__ __m128i _mm_cmpgt_epi16(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<8;i++) r.u16[i] = (ua.i16[i] > ub.i16[i]) ? 0xffffu : 0u;
	return(r.v);
}

static __inline__ __m128i _mm_cmpgt_epi32(__m128i a, __m128i b) {
	__m128i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<4;i++) r.u32[i] = (ua.i32[i] > ub.i32[i]) ? 0xffffffffu : 0u;
	return(r.v);
}

static __inline__ int _mm_movemask_epi8(__m128i a) {
	__m128i_u ua;
	int i;
	int m = 0;
	ua.v = a;
	for(i=0;i<16;i++)
		if((ua.u8[i] & 0x80u) != 0) m |= (1 << i);
	return(m);
}

static __inline__ __m128i _mm_slli_epi32(__m128i a, int count) {
	__m128i_u ua, r;
	int i;
	unsigned c = (count < 0) ? 0u : (unsigned)count;
	if(c > 31u) c = 31u;
	ua.v = a;
	for(i=0;i<4;i++) r.u32[i] = ua.u32[i] << c;
	return(r.v);
}

static __inline__ __m128i _mm_srli_epi32(__m128i a, int count) {
	__m128i_u ua, r;
	int i;
	unsigned c = (count < 0) ? 0u : (unsigned)count;
	if(c > 31u) c = 31u;
	ua.v = a;
	for(i=0;i<4;i++) r.u32[i] = ua.u32[i] >> c;
	return(r.v);
}

static __inline__ __m128i _mm_srai_epi32(__m128i a, int count) {
	__m128i_u ua, r;
	int i;
	unsigned c = (count < 0) ? 0u : (unsigned)count;
	if(c > 31u) c = 31u;
	ua.v = a;
	for(i=0;i<4;i++) r.i32[i] = ua.i32[i] >> c;
	return(r.v);
}

static __inline__ __m128i _mm_shuffle_epi32(__m128i a, int imm8) {
	__m128i_u ua, r;
	int i;
	ua.v = a;
	for(i=0;i<4;i++) {
		unsigned sel = ((unsigned)imm8 >> (i * 2)) & 0x3u;
		r.i32[i] = ua.i32[sel];
	}
	return(r.v);
}

static __inline__ __m128i _mm_cvtsi32_si128(int a) {
	__m128i_u r;
	r.u64[0] = 0u;
	r.u64[1] = 0u;
	r.i32[0] = a;
	return(r.v);
}

static __inline__ int _mm_cvtsi128_si32(__m128i a) {
	__m128i_u ua;
	ua.v = a;
	return(ua.i32[0]);
}

static __inline__ __m128d _mm_add_pd(__m128d a, __m128d b) {
	__m128d_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.d[0] = ua.d[0] + ub.d[0];
	r.d[1] = ua.d[1] + ub.d[1];
	return(r.v);
}

static __inline__ __m128d _mm_sub_pd(__m128d a, __m128d b) {
	__m128d_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.d[0] = ua.d[0] - ub.d[0];
	r.d[1] = ua.d[1] - ub.d[1];
	return(r.v);
}

static __inline__ __m128d _mm_mul_pd(__m128d a, __m128d b) {
	__m128d_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.d[0] = ua.d[0] * ub.d[0];
	r.d[1] = ua.d[1] * ub.d[1];
	return(r.v);
}

static __inline__ __m128d _mm_div_pd(__m128d a, __m128d b) {
	__m128d_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.d[0] = ua.d[0] / ub.d[0];
	r.d[1] = ua.d[1] / ub.d[1];
	return(r.v);
}

#endif
