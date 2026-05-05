#define HOST_TEST 1

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Pull just the helper functions out of ac97.c.  Mirroring them keeps
 * the test self-contained — the full driver pulls in PCI, IRQ, DMA,
 * and audio framework infrastructure that has no host equivalent.
 */
#define AC97_BDL_F_BUP  0x4000
#define AC97_BDL_F_IOC  0x8000

typedef struct ac97_bdl_entry {
	uint32_t buf_phys;
	uint16_t samples;
	uint16_t flags;
} __attribute__((packed)) ac97_bdl_entry_t;

static void ac97_build_bdl_entry(ac97_bdl_entry_t *entry, uint32_t buf_phys,
                                 uint16_t samples, int ioc)
{
	if (entry == NULL) {
		return;
	}
	entry->buf_phys = buf_phys;
	entry->samples  = samples;
	entry->flags    = (uint16_t)(ioc ? AC97_BDL_F_IOC : 0);
}

static uint16_t ac97_mixer_volume(uint8_t left, uint8_t right, int mute)
{
	uint16_t v;

	if (left > 0x3F)  left = 0x3F;
	if (right > 0x3F) right = 0x3F;
	v = (uint16_t)((left & 0x3F) | ((right & 0x3F) << 8));
	if (mute) {
		v |= 0x8000;
	}
	return v;
}

static uint16_t ac97_encode_rate(uint32_t hz, int has_vra)
{
	if (!has_vra) {
		return 48000;
	}
	if (hz < 8000)   hz = 8000;
	if (hz > 48000)  hz = 48000;
	return (uint16_t)hz;
}

/* ------------------------------------------------------------------- */

static void test_bdl_entry_layout_is_packed_8_bytes(void) {
	assert(sizeof(ac97_bdl_entry_t) == 8);
}

static void test_bdl_entry_no_ioc(void) {
	ac97_bdl_entry_t e;
	memset(&e, 0xAA, sizeof(e));
	ac97_build_bdl_entry(&e, 0x12345678U, 4096, 0);
	assert(e.buf_phys == 0x12345678U);
	assert(e.samples == 4096);
	assert(e.flags == 0);
}

static void test_bdl_entry_ioc_flag(void) {
	ac97_bdl_entry_t e;
	ac97_build_bdl_entry(&e, 0x10000000U, 256, 1);
	assert(e.flags == AC97_BDL_F_IOC);
}

static void test_bdl_entry_null_pointer_is_safe(void) {
	ac97_build_bdl_entry(NULL, 0, 0, 0);
}

static void test_mixer_volume_basic(void) {
	assert(ac97_mixer_volume(0, 0, 0) == 0x0000);
	assert(ac97_mixer_volume(0x10, 0x20, 0) == 0x2010);
	assert(ac97_mixer_volume(0, 0, 1) == 0x8000);
}

static void test_mixer_volume_clamps_above_six_bits(void) {
	uint16_t v = ac97_mixer_volume(0xFF, 0xFF, 0);
	assert((v & 0x3F) == 0x3F);
	assert(((v >> 8) & 0x3F) == 0x3F);
}

static void test_encode_rate_no_vra_locks_to_48k(void) {
	assert(ac97_encode_rate(8000, 0) == 48000);
	assert(ac97_encode_rate(48000, 0) == 48000);
	assert(ac97_encode_rate(44100, 0) == 48000);
}

static void test_encode_rate_with_vra_passes_through(void) {
	assert(ac97_encode_rate(44100, 1) == 44100);
	assert(ac97_encode_rate(22050, 1) == 22050);
}

static void test_encode_rate_with_vra_clamps_extremes(void) {
	assert(ac97_encode_rate(1000, 1) == 8000);
	assert(ac97_encode_rate(96000, 1) == 48000);
}

int main(void) {
	test_bdl_entry_layout_is_packed_8_bytes();
	test_bdl_entry_no_ioc();
	test_bdl_entry_ioc_flag();
	test_bdl_entry_null_pointer_is_safe();
	test_mixer_volume_basic();
	test_mixer_volume_clamps_above_six_bits();
	test_encode_rate_no_vra_locks_to_48k();
	test_encode_rate_with_vra_passes_through();
	test_encode_rate_with_vra_clamps_extremes();
	puts("host_test_ac97: PASS");
	return 0;
}
