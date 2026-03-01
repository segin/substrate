#include <immintrin.h>
#include <stdint.h>

int main(void) {
	__m128i a = _mm_set1_epi8(0x7f);
	__m128i b = _mm_set1_epi8(1);
	__m128i c = _mm_add_epi8(a, b);
	__m256i va = _mm256_setr_epi32(1, 2, 3, 4, 5, 6, 7, 8);
	__m256i vb = _mm256_set1_epi32(7);
	__m256i vc = _mm256_cmpeq_epi32(va, vb);
	uint32_t out32[8];
	int m;

	m = _mm_movemask_epi8(c);
	if(m != 65535) return(1);
	_mm256_storeu_si256((__m256i *)out32, vc);
	if(out32[6] != 0xffffffffu) return(2);
	if(out32[0] != 0u) return(3);

	return(0);
}
