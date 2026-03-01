#ifndef _SUBSTRATE_IMMINTRIN_H_
#define _SUBSTRATE_IMMINTRIN_H_

#include <stdint.h>
#include <mmintrin.h>
#include <xmmintrin.h>
#include <emmintrin.h>

/*
 * AVX/AVX2/AVX-512 intrinsics are not implemented in this header yet.
 * Keep type shims so code guarded by feature macros can still parse.
 */
typedef struct __attribute__((aligned(32))) {
	int64_t q[4];
} __m256i;

typedef struct __attribute__((aligned(32))) {
	float f[8];
} __m256;

typedef struct __attribute__((aligned(32))) {
	double d[4];
} __m256d;

typedef union {
	__m256i v;
	uint8_t u8[32];
	int8_t i8[32];
	uint16_t u16[16];
	int16_t i16[16];
	uint32_t u32[8];
	int32_t i32[8];
	uint64_t u64[4];
	int64_t i64[4];
} __m256i_u;

static __inline__ __m256i _mm256_setzero_si256(void) {
	__m256i_u r;
	int i;
	for(i=0;i<4;i++) r.u64[i] = 0u;
	return(r.v);
}

static __inline__ __m256i _mm256_loadu_si256(const __m256i *p) {
	return(*p);
}

static __inline__ __m256i _mm256_load_si256(const __m256i *p) {
	return(*p);
}

static __inline__ void _mm256_storeu_si256(__m256i *p, __m256i a) {
	*p = a;
}

static __inline__ void _mm256_store_si256(__m256i *p, __m256i a) {
	*p = a;
}

static __inline__ __m256i _mm256_set1_epi32(int x) {
	__m256i_u r;
	int i;
	for(i=0;i<8;i++) r.i32[i] = x;
	return(r.v);
}

static __inline__ __m256i _mm256_setr_epi32(int e0, int e1, int e2, int e3, int e4, int e5, int e6, int e7) {
	__m256i_u r;
	r.i32[0] = e0;
	r.i32[1] = e1;
	r.i32[2] = e2;
	r.i32[3] = e3;
	r.i32[4] = e4;
	r.i32[5] = e5;
	r.i32[6] = e6;
	r.i32[7] = e7;
	return(r.v);
}

static __inline__ __m256i _mm256_setr_epi8(
	signed char e00, signed char e01, signed char e02, signed char e03,
	signed char e04, signed char e05, signed char e06, signed char e07,
	signed char e08, signed char e09, signed char e10, signed char e11,
	signed char e12, signed char e13, signed char e14, signed char e15,
	signed char e16, signed char e17, signed char e18, signed char e19,
	signed char e20, signed char e21, signed char e22, signed char e23,
	signed char e24, signed char e25, signed char e26, signed char e27,
	signed char e28, signed char e29, signed char e30, signed char e31) {
	__m256i_u r;
	r.i8[0] = e00; r.i8[1] = e01; r.i8[2] = e02; r.i8[3] = e03;
	r.i8[4] = e04; r.i8[5] = e05; r.i8[6] = e06; r.i8[7] = e07;
	r.i8[8] = e08; r.i8[9] = e09; r.i8[10] = e10; r.i8[11] = e11;
	r.i8[12] = e12; r.i8[13] = e13; r.i8[14] = e14; r.i8[15] = e15;
	r.i8[16] = e16; r.i8[17] = e17; r.i8[18] = e18; r.i8[19] = e19;
	r.i8[20] = e20; r.i8[21] = e21; r.i8[22] = e22; r.i8[23] = e23;
	r.i8[24] = e24; r.i8[25] = e25; r.i8[26] = e26; r.i8[27] = e27;
	r.i8[28] = e28; r.i8[29] = e29; r.i8[30] = e30; r.i8[31] = e31;
	return(r.v);
}

static __inline__ __m256i _mm256_add_epi64(__m256i a, __m256i b) {
	__m256i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<4;i++) r.i64[i] = ua.i64[i] + ub.i64[i];
	return(r.v);
}

static __inline__ __m256i _mm256_xor_si256(__m256i a, __m256i b) {
	__m256i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<4;i++) r.u64[i] = ua.u64[i] ^ ub.u64[i];
	return(r.v);
}

static __inline__ __m256i _mm256_slli_epi64(__m256i a, int count) {
	__m256i_u ua, r;
	int i;
	unsigned c = (count < 0) ? 0u : (unsigned)count;
	if(c > 63u) c = 63u;
	ua.v = a;
	for(i=0;i<4;i++) r.u64[i] = ua.u64[i] << c;
	return(r.v);
}

static __inline__ __m256i _mm256_srli_epi64(__m256i a, int count) {
	__m256i_u ua, r;
	int i;
	unsigned c = (count < 0) ? 0u : (unsigned)count;
	if(c > 63u) c = 63u;
	ua.v = a;
	for(i=0;i<4;i++) r.u64[i] = ua.u64[i] >> c;
	return(r.v);
}

static __inline__ __m256i _mm256_cmpeq_epi32(__m256i a, __m256i b) {
	__m256i_u ua, ub, r;
	int i;
	ua.v = a;
	ub.v = b;
	for(i=0;i<8;i++) r.u32[i] = (ua.i32[i] == ub.i32[i]) ? 0xffffffffu : 0u;
	return(r.v);
}

static __inline__ __m256i _mm256_shuffle_epi8(__m256i a, __m256i mask) {
	__m256i_u ua, um, r;
	int lane, i;
	ua.v = a;
	um.v = mask;
	for(lane=0;lane<2;lane++) {
		int base = lane * 16;
		for(i=0;i<16;i++) {
			unsigned char sel = um.u8[base + i];
			if((sel & 0x80u) != 0) {
				r.u8[base + i] = 0u;
			} else {
				r.u8[base + i] = ua.u8[base + (sel & 0x0fu)];
			}
		}
	}
	return(r.v);
}

static __inline__ __m256i _mm256_permute4x64_epi64(__m256i a, int imm8) {
	__m256i_u ua, r;
	int i;
	ua.v = a;
	for(i=0;i<4;i++) {
		unsigned sel = ((unsigned)imm8 >> (i * 2)) & 0x3u;
		r.i64[i] = ua.i64[sel];
	}
	return(r.v);
}

static __inline__ __m128i _mm256_castsi256_si128(__m256i a) {
	__m256i_u ua;
	__m128i_u r;
	ua.v = a;
	r.i64[0] = ua.i64[0];
	r.i64[1] = ua.i64[1];
	return(r.v);
}

static __inline__ unsigned long long _lzcnt_u64(unsigned long long x) {
	unsigned long long n = 0;
	unsigned long long bit = 1ULL << 63;
	while(bit != 0 && (x & bit) == 0) {
		n++;
		bit >>= 1;
	}
	return(n);
}

static __inline__ unsigned long long _popcnt64(unsigned long long x) {
	unsigned long long n = 0;
	while(x != 0) {
		n += x & 1ULL;
		x >>= 1;
	}
	return(n);
}

static __inline__ unsigned long long _pext_u64(unsigned long long x, unsigned long long mask) {
	unsigned long long out = 0;
	unsigned long long bit = 1ULL;
	while(mask != 0) {
		unsigned long long lsb = mask & (~mask + 1ULL);
		if((x & lsb) != 0) out |= bit;
		mask &= (mask - 1ULL);
		bit <<= 1;
	}
	return(out);
}

static __inline__ unsigned long long _pdep_u64(unsigned long long x, unsigned long long mask) {
	unsigned long long out = 0;
	unsigned long long bit = 1ULL;
	while(mask != 0) {
		unsigned long long lsb = mask & (~mask + 1ULL);
		if((x & bit) != 0) out |= lsb;
		mask &= (mask - 1ULL);
		bit <<= 1;
	}
	return(out);
}

#endif
