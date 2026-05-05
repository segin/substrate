#define HOST_TEST 1

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SB16_DSP_MODE_STEREO  0x20
#define SB16_DSP_MODE_SIGNED  0x10

static uint8_t sb16_encode_irq(int irq)
{
	switch (irq) {
	case 2:  return 0x01;
	case 5:  return 0x02;
	case 7:  return 0x04;
	case 10: return 0x08;
	default: return 0;
	}
}

static uint8_t sb16_encode_dma(int dma8, int dma16)
{
	uint8_t v = 0;

	if (dma8 >= 0 && dma8 <= 3) {
		v |= (uint8_t)(1u << dma8);
	}
	if (dma16 >= 5 && dma16 <= 7) {
		v |= (uint8_t)(1u << dma16);
	}
	return v;
}

static uint8_t sb16_time_constant(uint32_t hz)
{
	uint32_t tc;

	if (hz == 0) {
		return 0;
	}
	tc = 1000000U / hz;
	if (tc >= 256U) {
		return 0;
	}
	return (uint8_t)(256U - tc);
}

static uint8_t sb16_play_mode(uint32_t channels, int is_signed)
{
	uint8_t mode = 0;
	if (channels >= 2) {
		mode |= SB16_DSP_MODE_STEREO;
	}
	if (is_signed) {
		mode |= SB16_DSP_MODE_SIGNED;
	}
	return mode;
}

/* ------------------------------------------------------------------- */

static void test_irq_encoding(void) {
	assert(sb16_encode_irq(2) == 0x01);
	assert(sb16_encode_irq(5) == 0x02);
	assert(sb16_encode_irq(7) == 0x04);
	assert(sb16_encode_irq(10) == 0x08);
	/* Unsupported IRQ lines yield zero. */
	assert(sb16_encode_irq(3) == 0);
	assert(sb16_encode_irq(11) == 0);
	assert(sb16_encode_irq(-1) == 0);
}

static void test_dma_encoding(void) {
	/* default 8-bit ch1 + 16-bit ch5 */
	assert(sb16_encode_dma(1, 5) == ((1u << 1) | (1u << 5)));
	/* only 8-bit */
	assert(sb16_encode_dma(0, -1) == (1u << 0));
	/* only 16-bit */
	assert(sb16_encode_dma(-1, 7) == (1u << 7));
	/* invalid channel numbers ignored */
	assert(sb16_encode_dma(4, 4) == 0);
	assert(sb16_encode_dma(8, 8) == 0);
}

static void test_time_constant_canonical_rates(void) {
	/* 22050 Hz: 1e6 / 22050 = ~45 → tc = 256 - 45 = 211 */
	assert(sb16_time_constant(22050) == (uint8_t)(256U - 1000000U / 22050U));
	/* 44100 Hz: 1e6 / 44100 = 22 → tc = 234 */
	assert(sb16_time_constant(44100) == (uint8_t)(256U - 1000000U / 44100U));
	/* 11025 Hz: divisor 90 → tc = 166 */
	assert(sb16_time_constant(11025) == (uint8_t)(256U - 1000000U / 11025U));
}

static void test_time_constant_zero_safe(void) {
	assert(sb16_time_constant(0) == 0);
}

static void test_time_constant_too_low_returns_zero(void) {
	/* Very low rates make 1e6 / hz exceed 255 → unsupported, returns 0. */
	assert(sb16_time_constant(100) == 0);
}

static void test_play_mode_combinations(void) {
	/* mono, unsigned (default 8-bit-style payloads): both bits clear. */
	assert(sb16_play_mode(1, 0) == 0);
	/* mono, signed */
	assert(sb16_play_mode(1, 1) == SB16_DSP_MODE_SIGNED);
	/* stereo, unsigned */
	assert(sb16_play_mode(2, 0) == SB16_DSP_MODE_STEREO);
	/* stereo, signed (the canonical SB16 16-bit mode) */
	assert(sb16_play_mode(2, 1) == (SB16_DSP_MODE_STEREO | SB16_DSP_MODE_SIGNED));
	/* >2 channels: still flagged as stereo (driver only supports up to 2). */
	assert(sb16_play_mode(8, 1) == (SB16_DSP_MODE_STEREO | SB16_DSP_MODE_SIGNED));
}

int main(void) {
	test_irq_encoding();
	test_dma_encoding();
	test_time_constant_canonical_rates();
	test_time_constant_zero_safe();
	test_time_constant_too_low_returns_zero();
	test_play_mode_combinations();
	puts("host_test_sb16: PASS");
	return 0;
}
