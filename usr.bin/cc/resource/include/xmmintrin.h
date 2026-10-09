#ifndef _SUBSTRATE_XMMINTRIN_H_
#define _SUBSTRATE_XMMINTRIN_H_

#include <stdint.h>

typedef struct __attribute__((aligned(16))) {
	float f[4];
} __m128;

typedef union {
	__m128 v;
	float f[4];
	uint32_t u32[4];
	int32_t i32[4];
} __m128_u;

static __inline__ __m128 _mm_setzero_ps(void) {
	__m128_u r;
	int i;
	for(i=0;i<4;i++) r.u32[i] = 0u;
	return(r.v);
}

static __inline__ __m128 _mm_set1_ps(float x) {
	__m128_u r;
	int i;
	for(i=0;i<4;i++) r.f[i] = x;
	return(r.v);
}

static __inline__ __m128 _mm_set_ps(float e3, float e2, float e1, float e0) {
	__m128_u r;
	r.f[0] = e0;
	r.f[1] = e1;
	r.f[2] = e2;
	r.f[3] = e3;
	return(r.v);
}

static __inline__ __m128 _mm_setr_ps(float e0, float e1, float e2, float e3) {
	return(_mm_set_ps(e3, e2, e1, e0));
}

static __inline__ __m128 _mm_load_ps(const float *p) {
	__m128_u r;
	int i;
	for(i=0;i<4;i++) r.f[i] = p[i];
	return(r.v);
}

static __inline__ __m128 _mm_loadu_ps(const float *p) {
	return(_mm_load_ps(p));
}

static __inline__ void _mm_store_ps(float *p, __m128 a) {
	__m128_u ua;
	int i;
	ua.v = a;
	for(i=0;i<4;i++) p[i] = ua.f[i];
}

static __inline__ void _mm_storeu_ps(float *p, __m128 a) {
	_mm_store_ps(p, a);
}

static __inline__ __m128 _mm_add_ps(__m128 a, __m128 b) {
	__m128_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<4;i++) r.f[i] = ua.f[i] + ub.f[i];
	return(r.v);
}

static __inline__ __m128 _mm_sub_ps(__m128 a, __m128 b) {
	__m128_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<4;i++) r.f[i] = ua.f[i] - ub.f[i];
	return(r.v);
}

static __inline__ __m128 _mm_mul_ps(__m128 a, __m128 b) {
	__m128_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<4;i++) r.f[i] = ua.f[i] * ub.f[i];
	return(r.v);
}

static __inline__ __m128 _mm_div_ps(__m128 a, __m128 b) {
	__m128_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<4;i++) r.f[i] = ua.f[i] / ub.f[i];
	return(r.v);
}

static __inline__ int _mm_movemask_ps(__m128 a) {
	__m128_u ua;
	int i;
	int m = 0;
	ua.v = a;
	for(i=0;i<4;i++)
		if((ua.u32[i] & 0x80000000u) != 0) m |= (1 << i);
	return(m);
}

static __inline__ void _mm_pause(void) {
	__asm__ __volatile__("pause" : : : "memory");
}

#endif
