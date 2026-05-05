#define HOST_TEST 1

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Pure helpers from hda.c.  Mirrored here verbatim so the test stays
 * independent of the full driver's PCI/IRQ/DMA dependencies.
 */

#define HDA_VERB_GET_PARAMETER 0xF00
#define HDA_VERB_SET_CONV_FORMAT 0x200
#define HDA_VERB_SET_CONV_STREAM 0x706
#define HDA_BDL_F_IOC          0x01

typedef struct hda_bdl_entry {
	uint64_t buf_phys;
	uint32_t length;
	uint32_t flags;
} __attribute__((packed)) hda_bdl_entry_t;

static uint32_t hda_pack_verb(uint8_t cad, uint8_t nid, uint16_t verb,
                              uint16_t payload)
{
	uint32_t v = 0;
	v |= ((uint32_t)(cad & 0x0F)) << 28;
	v |= ((uint32_t)nid) << 20;
	if (verb >= 0xF00) {
		v |= ((uint32_t)(verb & 0x0FFF)) << 8;
		v |= (uint32_t)(payload & 0x00FF);
	} else {
		v |= ((uint32_t)(verb & 0xF00)) << 8;
		v |= (uint32_t)(payload & 0xFFFF);
	}
	return v;
}

static uint16_t hda_encode_format(uint32_t sample_rate,
                                  uint32_t bits_per_sample, uint32_t channels)
{
	uint16_t fmt = 0;
	uint16_t bits;
	uint16_t chan;
	uint32_t base_rate;
	uint16_t mult;
	uint16_t div;

	if (channels == 0 || channels > 16) return 0;
	chan = (uint16_t)((channels - 1) & 0x0F);
	switch (bits_per_sample) {
	case 8:  bits = 0; break;
	case 16: bits = 1; break;
	case 20: bits = 2; break;
	case 24: bits = 3; break;
	case 32: bits = 4; break;
	default: return 0;
	}
	if (sample_rate % 44100U == 0) {
		base_rate = 1;
		mult = (uint16_t)((sample_rate / 44100U) - 1);
		div = 0;
	} else if (sample_rate % 48000U == 0) {
		base_rate = 0;
		mult = (uint16_t)((sample_rate / 48000U) - 1);
		div = 0;
	} else if (sample_rate == 22050U) {
		base_rate = 1; mult = 0; div = 1;
	} else if (sample_rate == 24000U || sample_rate == 32000U) {
		base_rate = 0; mult = 0;
		div = (sample_rate == 24000U) ? 1 : 0;
	} else {
		base_rate = 0; mult = 0; div = 0;
	}
	if (mult > 7) mult = 7;
	if (div > 7)  div = 7;

	fmt |= (uint16_t)(base_rate << 14);
	fmt |= (uint16_t)(mult << 11);
	fmt |= (uint16_t)(div << 8);
	fmt |= (uint16_t)(bits << 4);
	fmt |= chan;
	return fmt;
}

static void hda_build_bdl_entry(hda_bdl_entry_t *e, uint64_t buf_phys,
                                uint32_t length, int ioc)
{
	if (e == NULL) return;
	e->buf_phys = buf_phys;
	e->length   = length;
	e->flags    = (uint32_t)(ioc ? HDA_BDL_F_IOC : 0);
}

/* ------------------------------------------------------------------- */

static void test_pack_verb_long_form(void) {
	/* GET_PARAMETER (0xF00) on cad=0, nid=1, payload=VENDOR_ID(0x00) */
	uint32_t v = hda_pack_verb(0, 1, HDA_VERB_GET_PARAMETER, 0);
	/* cad=0 → bits 28..31 = 0, nid=1 → bits 20..27 = 0x01, verb=0xF00
	 * → bits 8..19 = 0xF00, payload=0 */
	assert(v == ((1u << 20) | (0xF00u << 8)));
}

static void test_pack_verb_long_form_payload_truncated_to_8_bits(void) {
	/* Long-form payload is 8 bits; high bits should be discarded. */
	uint32_t v = hda_pack_verb(0, 0, HDA_VERB_GET_PARAMETER, 0x1FF);
	/* low byte = 0xFF only */
	assert((v & 0xFF) == 0xFF);
}

static void test_pack_verb_short_form_full_16bit_payload(void) {
	/* SET_CONVERTER_FORMAT (0x200) is short-form; payload preserved
	 * as 16 bits. */
	uint32_t v = hda_pack_verb(0, 1, HDA_VERB_SET_CONV_FORMAT, 0xCAFE);
	assert((v & 0xFFFF) == 0xCAFE);
}

static void test_pack_verb_codec_address_field(void) {
	uint32_t v = hda_pack_verb(7, 0x55, HDA_VERB_GET_PARAMETER, 0);
	assert(((v >> 28) & 0xF) == 7);
	assert(((v >> 20) & 0xFF) == 0x55);
}

static void test_pack_verb_codec_address_clamped_to_4_bits(void) {
	uint32_t v = hda_pack_verb(0xFF, 0, HDA_VERB_GET_PARAMETER, 0);
	assert(((v >> 28) & 0xF) == 0xF);
}

static void test_format_48k_stereo_16bit(void) {
	uint16_t fmt = hda_encode_format(48000, 16, 2);
	/* BASE=0, MULT=0, DIV=0, BITS=1 (16-bit), CHAN-1=1 → 0x0011 */
	assert(fmt == 0x0011);
}

static void test_format_44_1k_stereo_16bit(void) {
	/* BASE=1, MULT=0, DIV=0, BITS=1, CHAN-1=1 → 0x4011 */
	uint16_t fmt = hda_encode_format(44100, 16, 2);
	assert(fmt == 0x4011);
}

static void test_format_88_2k_uses_mult(void) {
	/* 88200 = 44100*2 → BASE=1, MULT=1 → bits 11..13 = 1 */
	uint16_t fmt = hda_encode_format(88200, 16, 2);
	assert(((fmt >> 14) & 1) == 1);          /* BASE=1 */
	assert(((fmt >> 11) & 7) == 1);          /* MULT=1 */
}

static void test_format_22050_uses_div(void) {
	uint16_t fmt = hda_encode_format(22050, 16, 2);
	assert(((fmt >> 14) & 1) == 1);          /* BASE=1 */
	assert(((fmt >> 8) & 7) == 1);           /* DIV=1 */
}

static void test_format_8bit_mono(void) {
	uint16_t fmt = hda_encode_format(48000, 8, 1);
	/* BITS=0, CHAN-1=0 → 0x0000 */
	assert(fmt == 0x0000);
}

static void test_format_24bit_marks_correct_bits_field(void) {
	uint16_t fmt = hda_encode_format(48000, 24, 2);
	assert(((fmt >> 4) & 7) == 3);
}

static void test_format_unsupported_rejected(void) {
	assert(hda_encode_format(48000, 12, 2) == 0);   /* odd width */
	assert(hda_encode_format(48000, 16, 0) == 0);   /* zero channels */
	assert(hda_encode_format(48000, 16, 99) == 0);  /* too many ch */
}

static void test_bdl_entry_layout_is_packed_16_bytes(void) {
	assert(sizeof(hda_bdl_entry_t) == 16);
}

static void test_bdl_build_with_and_without_ioc(void) {
	hda_bdl_entry_t e;
	hda_build_bdl_entry(&e, 0x100000000ULL, 4096, 0);
	assert(e.buf_phys == 0x100000000ULL);
	assert(e.length == 4096);
	assert(e.flags == 0);
	hda_build_bdl_entry(&e, 0x200000000ULL, 8192, 1);
	assert(e.flags == HDA_BDL_F_IOC);
}

static void test_bdl_build_null_safe(void) {
	hda_build_bdl_entry(NULL, 0, 0, 0);
}

int main(void) {
	test_pack_verb_long_form();
	test_pack_verb_long_form_payload_truncated_to_8_bits();
	test_pack_verb_short_form_full_16bit_payload();
	test_pack_verb_codec_address_field();
	test_pack_verb_codec_address_clamped_to_4_bits();
	test_format_48k_stereo_16bit();
	test_format_44_1k_stereo_16bit();
	test_format_88_2k_uses_mult();
	test_format_22050_uses_div();
	test_format_8bit_mono();
	test_format_24bit_marks_correct_bits_field();
	test_format_unsupported_rejected();
	test_bdl_entry_layout_is_packed_16_bytes();
	test_bdl_build_with_and_without_ioc();
	test_bdl_build_null_safe();
	puts("host_test_hda: PASS");
	return 0;
}
