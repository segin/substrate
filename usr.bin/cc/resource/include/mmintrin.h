#ifndef _SUBSTRATE_MMINTRIN_H_
#define _SUBSTRATE_MMINTRIN_H_

#include <stdint.h>

typedef struct __attribute__((aligned(8))) {
	int64_t i64;
} __m64;

typedef union {
	__m64 v;
	int8_t i8[8];
	uint8_t u8[8];
	int16_t i16[4];
	uint16_t u16[4];
	int32_t i32[2];
	uint32_t u32[2];
	int64_t i64;
	uint64_t u64;
} __m64_u;

static __inline__ __m64 _mm_setzero_si64(void) {
	__m64_u r;
	r.u64 = 0;
	return(r.v);
}

static __inline__ __m64 _mm_set_pi32(int e1, int e0) {
	__m64_u r;
	r.i32[0] = e0;
	r.i32[1] = e1;
	return(r.v);
}

static __inline__ __m64 _mm_cvtsi32_si64(int a) {
	__m64_u r;
	r.i32[0] = a;
	r.i32[1] = 0;
	return(r.v);
}

static __inline__ int _mm_cvtsi64_si32(__m64 a) {
	__m64_u ua;
	ua.v = a;
	return(ua.i32[0]);
}

static __inline__ __m64 _mm_add_pi32(__m64 a, __m64 b) {
	__m64_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.i32[0] = ua.i32[0] + ub.i32[0];
	r.i32[1] = ua.i32[1] + ub.i32[1];
	return(r.v);
}

static __inline__ __m64 _mm_sub_pi32(__m64 a, __m64 b) {
	__m64_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.i32[0] = ua.i32[0] - ub.i32[0];
	r.i32[1] = ua.i32[1] - ub.i32[1];
	return(r.v);
}

static __inline__ __m64 _mm_and_si64(__m64 a, __m64 b) {
	__m64_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.u64 = ua.u64 & ub.u64;
	return(r.v);
}

static __inline__ __m64 _mm_or_si64(__m64 a, __m64 b) {
	__m64_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.u64 = ua.u64 | ub.u64;
	return(r.v);
}

static __inline__ __m64 _mm_xor_si64(__m64 a, __m64 b) {
	__m64_u ua, ub, r;
	ua.v = a;
	ub.v = b;
	r.u64 = ua.u64 ^ ub.u64;
	return(r.v);
}

static __inline__ void _mm_empty(void) {
	__asm__ __volatile__("emms" : : : "memory");
}

#endif
