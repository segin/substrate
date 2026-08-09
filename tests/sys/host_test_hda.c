#define HOST_TEST 1

#include <assert.h>
#include <errno.h>
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
#define HDA_VERB_SET_AMP_GAIN_MUTE 0x300
#define HDA_VERB_SET_CONN_SELECT 0x701
#define HDA_VERB_SET_POWER_STATE 0x705
#define HDA_VERB_SET_CONV_STREAM 0x706
#define HDA_VERB_SET_PIN_WIDGET_CONTROL 0x707
#define HDA_VERB_SET_EAPD_BTL  0x70C
#define HDA_VERB_GET_CONV_FORMAT 0xA00
#define HDA_BDL_F_IOC          0x01

typedef struct hda_bdl_entry {
	uint64_t buf_phys;
	uint32_t length;
	uint32_t flags;
} __attribute__((packed)) hda_bdl_entry_t;

static int hda_verb_is_short(uint16_t verb)
{
	if ((verb & 0x00FF) != 0) {
		return 0;
	}
	switch ((verb >> 8) & 0x0F) {
	case 0x2: case 0x3: case 0xA: case 0xB:
		return 1;
	default:
		return 0;
	}
}

static uint32_t hda_pack_verb(uint8_t cad, uint8_t nid, uint16_t verb,
                              uint16_t payload)
{
	uint32_t v = 0;
	v |= ((uint32_t)(cad & 0x0F)) << 28;
	v |= ((uint32_t)nid) << 20;
	if (hda_verb_is_short(verb)) {
		v |= ((uint32_t)(verb & 0xF00)) << 8;
		v |= (uint32_t)(payload & 0xFFFF);
	} else {
		v |= ((uint32_t)(verb & 0x0FFF)) << 8;
		v |= (uint32_t)(payload & 0x00FF);
	}
	return v;
}

#define HDA_FMT_BASE_SHIFT 14
#define HDA_FMT_MULT_SHIFT 11
#define HDA_FMT_DIV_SHIFT  8
#define HDA_FMT_BITS_SHIFT 4

static const struct hda_rate_enc {
	uint32_t rate;
	uint8_t  base;
	uint8_t  mult;
	uint8_t  div;
} hda_rate_tab[] = {
	/* 48 kHz base */
	{      6000, 0, 0, 7 }, {      8000, 0, 0, 5 }, {   9600, 0, 0, 4 },
	{     12000, 0, 0, 3 }, {     16000, 0, 0, 2 }, {  18000, 0, 2, 7 },
	{     19200, 0, 1, 4 }, {     24000, 0, 0, 1 }, {  28800, 0, 2, 4 },
	{     32000, 0, 1, 2 }, {     36000, 0, 2, 3 }, {  38400, 0, 3, 4 },
	{     48000, 0, 0, 0 }, {     64000, 0, 3, 2 }, {  72000, 0, 2, 1 },
	{     96000, 0, 1, 0 }, {    144000, 0, 2, 0 }, { 192000, 0, 3, 0 },
	/* 44.1 kHz base */
	{      8820, 1, 0, 4 }, {     11025, 1, 0, 3 }, {  12600, 1, 1, 6 },
	{     14700, 1, 0, 2 }, {     17640, 1, 1, 4 }, {  18900, 1, 2, 6 },
	{     22050, 1, 0, 1 }, {     25200, 1, 3, 6 }, {  26460, 1, 2, 4 },
	{     29400, 1, 1, 2 }, {     33075, 1, 2, 3 }, {  35280, 1, 3, 4 },
	{     44100, 1, 0, 0 }, {     58800, 1, 3, 2 }, {  66150, 1, 2, 1 },
	{     88200, 1, 1, 0 }, {    132300, 1, 2, 0 }, { 176400, 1, 3, 0 },
};

static int hda_encode_format(uint32_t sample_rate, uint32_t bits_per_sample,
                             uint32_t channels, uint16_t *out)
{
	uint16_t bits;
	size_t i;

	if (out == NULL || channels == 0 || channels > 16) return -EINVAL;
	switch (bits_per_sample) {
	case 8:  bits = 0; break;
	case 16: bits = 1; break;
	case 20: bits = 2; break;
	case 24: bits = 3; break;
	case 32: bits = 4; break;
	default: return -EINVAL;
	}
	for (i = 0; i < sizeof(hda_rate_tab) / sizeof(hda_rate_tab[0]); i++) {
		const struct hda_rate_enc *r = &hda_rate_tab[i];

		if (r->rate != sample_rate) continue;
		*out = (uint16_t)(((uint16_t)r->base << HDA_FMT_BASE_SHIFT) |
		                  ((uint16_t)r->mult << HDA_FMT_MULT_SHIFT) |
		                  ((uint16_t)r->div  << HDA_FMT_DIV_SHIFT)  |
		                  (bits << HDA_FMT_BITS_SHIFT) |
		                  (uint16_t)((channels - 1) & 0x0F));
		return 0;
	}
	return -EINVAL;
}

static void hda_build_bdl_entry(hda_bdl_entry_t *e, uint64_t buf_phys,
                                uint32_t length, int ioc)
{
	if (e == NULL) return;
	e->buf_phys = buf_phys;
	e->length   = length;
	e->flags    = (uint32_t)(ioc ? HDA_BDL_F_IOC : 0);
}

/*
 * Connection list walker, mirrored from hda.c with the CORB round trip
 * replaced by a table of canned responses.  Entry width, the range flag
 * position and the request-index stride all depend on the list's form
 * (spec figure 51 / 7.3.3.3), and getting any of them wrong yields an
 * index that still looks plausible -- it just selects the wrong source
 * and unmutes the wrong input amp.
 */
#define HDA_MAX_CONNS 32

static uint32_t conn_resp[64];
static int conn_len;
static int conn_is_long;

static int hda_conn_list(uint8_t *conns, int max)
{
	int per = conn_is_long ? 2 : 4;
	uint16_t nmask = conn_is_long ? 0x7FFF : 0x7F;
	uint16_t rmask = conn_is_long ? 0x8000 : 0x80;
	uint16_t prev = 0;
	int n = 0, i;

	for (i = 0; i < conn_len && n < max; i += per) {
		uint32_t resp = conn_resp[i];
		int j;

		for (j = 0; j < per && (i + j) < conn_len && n < max; j++) {
			int shift = j * (conn_is_long ? 16 : 8);
			uint16_t raw = (uint16_t)((resp >> shift) &
			                          (conn_is_long ? 0xFFFFU : 0xFFU));
			uint16_t cnid = raw & nmask;
			uint16_t first;

			if (cnid == 0 || cnid > 0xFF) continue;
			if ((raw & rmask) == 0 || prev == 0 || prev >= cnid)
				first = cnid;
			else
				first = (uint16_t)(prev + 1);
			while (first <= cnid && n < max)
				conns[n++] = (uint8_t)first, first++;
			prev = cnid;
		}
	}
	return n;
}

static int conn_index_of(const uint8_t *c, int n, uint8_t target) {
	int i;
	for (i = 0; i < n; i++) if (c[i] == target) return i;
	return -1;
}

static void conn_setup(int is_long, int len) {
	memset(conn_resp, 0, sizeof(conn_resp));
	conn_is_long = is_long;
	conn_len = len;
}

static void test_conn_short_form_plain(void) {
	uint8_t c[HDA_MAX_CONNS];
	int n;

	conn_setup(0, 4);
	conn_resp[0] = 0x05040302;
	n = hda_conn_list(c, HDA_MAX_CONNS);
	assert(n == 4);
	assert(c[0] == 2 && c[1] == 3 && c[2] == 4 && c[3] == 5);
}

static void test_conn_short_form_spans_responses(void) {
	uint8_t c[HDA_MAX_CONNS];
	int n, i;

	/* Four entries per response; index 4 must be a second request. */
	conn_setup(0, 8);
	conn_resp[0] = 0x04030201;
	conn_resp[4] = 0x08070605;
	n = hda_conn_list(c, HDA_MAX_CONNS);
	assert(n == 8);
	for (i = 0; i < 8; i++) assert(c[i] == (uint8_t)(i + 1));
}

/* "the number of entries beyond the end of the list would be reported
 * as 0's" -- padding must not become connections. */
static void test_conn_zero_padding_ignored(void) {
	uint8_t c[HDA_MAX_CONNS];
	int n;

	conn_setup(0, 2);
	conn_resp[0] = 0x00000302;
	n = hda_conn_list(c, HDA_MAX_CONNS);
	assert(n == 2);
	assert(c[0] == 2 && c[1] == 3);
}

/* A range entry forms a run with the entry before it, and every NID in
 * between takes a real connection index. */
static void test_conn_short_form_range_expands(void) {
	uint8_t c[HDA_MAX_CONNS];
	int n;

	conn_setup(0, 2);
	conn_resp[0] = 0x00008502;   /* 2, then 5 with the range bit set */
	n = hda_conn_list(c, HDA_MAX_CONNS);
	assert(n == 4);
	assert(c[0] == 2 && c[1] == 3 && c[2] == 4 && c[3] == 5);
}

static void test_conn_long_form_plain(void) {
	uint8_t c[HDA_MAX_CONNS];
	int n;

	conn_setup(1, 2);
	conn_resp[0] = 0x00050002;
	n = hda_conn_list(c, HDA_MAX_CONNS);
	assert(n == 2);
	assert(c[0] == 2 && c[1] == 5);
}

/*
 * The exact shape the old code mis-parsed: a long-form range entry.  It
 * masked every entry to 8 bits regardless of form, so 0x8005 read as
 * plain NID 5 -- the range bit vanished into the NID and the run was
 * never expanded.  NID 5 then reported index 1 instead of 3, which is
 * the index handed to SET_CONNECTION_SELECT and to the amp Index field.
 */
static void test_conn_long_form_range_and_index(void) {
	uint8_t c[HDA_MAX_CONNS];
	int n;

	conn_setup(1, 2);
	conn_resp[0] = 0x80050002;
	n = hda_conn_list(c, HDA_MAX_CONNS);
	assert(n == 4);
	assert(c[0] == 2 && c[1] == 3 && c[2] == 4 && c[3] == 5);
	assert(conn_index_of(c, n, 5) == 3);
	assert(conn_index_of(c, n, 5) != 1);   /* what the old code returned */
}

/* Short-form NIDs are 7 bits: bit 7 is the range flag, so a raw compare
 * against the whole byte never matches a range entry. */
static void test_conn_range_flag_is_not_part_of_the_nid(void) {
	uint8_t c[HDA_MAX_CONNS];
	int n;

	conn_setup(0, 2);
	conn_resp[0] = 0x00008502;
	n = hda_conn_list(c, HDA_MAX_CONNS);
	assert(conn_index_of(c, n, 5) == 3);
	assert(conn_index_of(c, n, 0x85) == -1);
}

/* A codec describing a range backwards is malformed; take the entry as a
 * lone NID rather than looping or emitting garbage. */
static void test_conn_backwards_range_is_single_nid(void) {
	uint8_t c[HDA_MAX_CONNS];
	int n;

	conn_setup(0, 2);
	conn_resp[0] = 0x00008105;   /* 5, then 1 with the range bit set */
	n = hda_conn_list(c, HDA_MAX_CONNS);
	assert(n == 2);
	assert(c[0] == 5 && c[1] == 1);
}

static void test_conn_respects_max(void) {
	uint8_t c[4];
	int n;

	conn_setup(0, 2);
	conn_resp[0] = 0x0000FF02;   /* 2, range up to 127 */
	n = hda_conn_list(c, 4);
	assert(n == 4);
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

/* Decode an SDnFMT back to the link rate it actually asks the codec for.
 * A format that encodes "successfully" but decodes to a different rate is
 * the failure mode that matters: it is silent, and it plays the stream at
 * the wrong speed rather than failing. */
static uint32_t fmt_rate(uint16_t f) {
	uint32_t base = ((f >> HDA_FMT_BASE_SHIFT) & 1) ? 44100 : 48000;
	return base * (((f >> HDA_FMT_MULT_SHIFT) & 7) + 1) /
	              (((f >> HDA_FMT_DIV_SHIFT) & 7) + 1);
}

/*
 * Every rate in the table must round-trip, and no entry may use a
 * reserved MULT (spec table 40: 100b-111b reserved).
 */
static void test_format_every_table_rate_round_trips(void) {
	size_t i;

	for (i = 0; i < sizeof(hda_rate_tab) / sizeof(hda_rate_tab[0]); i++) {
		uint16_t fmt = 0xFFFF;
		uint32_t rate = hda_rate_tab[i].rate;

		assert(hda_encode_format(rate, 16, 2, &fmt) == 0);
		assert(fmt_rate(fmt) == rate);
		assert(((fmt >> HDA_FMT_MULT_SHIFT) & 7) <= 3);
		assert(((fmt >> HDA_FMT_BITS_SHIFT) & 7) == 1);
		assert((fmt & 0x0F) == 1);
	}
}

/*
 * The rates the old arithmetic encoder silently mis-encoded as 48 kHz.
 * 32 kHz is the interesting one: it is the only common rate that needs a
 * nonzero MULT *and* a nonzero DIV (48 kHz x2 / 3), which that encoder
 * could never produce.
 */
static void test_format_rates_the_old_encoder_got_wrong(void) {
	static const struct { uint32_t rate; uint16_t want; } t[] = {
		{  8000, 0x0511 },   /* 48k /6            */
		{ 11025, 0x4311 },   /* 44.1k /4          */
		{ 16000, 0x0211 },   /* 48k /3            */
		{ 32000, 0x0A11 },   /* 48k x2 /3         */
	};
	size_t i;

	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		uint16_t fmt = 0xFFFF;

		assert(hda_encode_format(t[i].rate, 16, 2, &fmt) == 0);
		assert(fmt == t[i].want);
		assert(fmt_rate(fmt) == t[i].rate);
		/* Not the 48 kHz stereo 16-bit encoding it used to collapse to. */
		assert(fmt != 0x0011);
	}
}

static void test_format_canonical_rates(void) {
	uint16_t fmt = 0xFFFF;

	assert(hda_encode_format(48000, 16, 2, &fmt) == 0);
	assert(fmt == 0x0011);   /* BASE=0 MULT=0 DIV=0 BITS=1 CHAN-1=1 */
	assert(hda_encode_format(44100, 16, 2, &fmt) == 0);
	assert(fmt == 0x4011);
	assert(hda_encode_format(88200, 16, 2, &fmt) == 0);
	assert(((fmt >> HDA_FMT_BASE_SHIFT) & 1) == 1);
	assert(((fmt >> HDA_FMT_MULT_SHIFT) & 7) == 1);
	assert(hda_encode_format(22050, 16, 2, &fmt) == 0);
	assert(((fmt >> HDA_FMT_BASE_SHIFT) & 1) == 1);
	assert(((fmt >> HDA_FMT_DIV_SHIFT) & 7) == 1);
}

/*
 * 48 kHz 8-bit mono is the all-zero encoding.  That is a legal format, so
 * the status has to come back separately -- when it was folded into the
 * return value this one combination was indistinguishable from
 * "unsupported" and set_params rejected it.
 */
static void test_format_8bit_mono_is_zero_but_valid(void) {
	uint16_t fmt = 0xFFFF;

	assert(hda_encode_format(48000, 8, 1, &fmt) == 0);
	assert(fmt == 0x0000);
}

static void test_format_24bit_marks_correct_bits_field(void) {
	uint16_t fmt = 0xFFFF;

	assert(hda_encode_format(48000, 24, 2, &fmt) == 0);
	assert(((fmt >> HDA_FMT_BITS_SHIFT) & 7) == 3);
}

static void test_format_unsupported_rejected(void) {
	uint16_t fmt = 0xBEEF;

	assert(hda_encode_format(48000, 12, 2, &fmt) == -EINVAL);  /* width */
	assert(hda_encode_format(48000, 16, 0, &fmt) == -EINVAL);  /* 0 ch  */
	assert(hda_encode_format(48000, 16, 99, &fmt) == -EINVAL); /* >16ch */
	assert(hda_encode_format(48000, 16, 2, NULL) == -EINVAL);
	/* Rates with no legal encoding must be refused, not approximated.
	 * 352800 = 44.1k x8 needs MULT=7, which is reserved; the old code
	 * clamped to it and handed the controller a reserved value. */
	assert(hda_encode_format(352800, 16, 2, &fmt) == -EINVAL);
	assert(hda_encode_format(37000, 16, 2, &fmt) == -EINVAL);
	assert(hda_encode_format(0, 16, 2, &fmt) == -EINVAL);
	/* Untouched on every rejection. */
	assert(fmt == 0xBEEF);
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

/*
 * The 0x7xx block is every command that configures a codec.  These are
 * 12-bit commands with an 8-bit payload; encoding them as short form put
 * the payload on top of the command bits and produced a verb the codec
 * would never act on.  The driver sent none of them, so nothing caught it.
 */
static void test_pack_verb_0x7xx_is_long_form(void)
{
	/* SET_CONVERTER_STREAM_CHANNEL, nid 2, stream tag 1 channel 0. */
	uint32_t v = hda_pack_verb(0, 2, HDA_VERB_SET_CONV_STREAM, 0x10);
	assert(v == ((2u << 20) | (0x706u << 8) | 0x10u));

	/* The old rule would have produced this instead -- verb bits 19:16
	 * = 7 and the payload smeared across 15:0. */
	assert(v != ((2u << 20) | ((0x706u & 0xF00u) << 8) | 0x10u));
}

static void test_pack_verb_power_pin_eapd_conn_long_form(void)
{
	assert(hda_pack_verb(0, 1, HDA_VERB_SET_POWER_STATE, 0x00) ==
	       ((1u << 20) | (0x705u << 8)));
	assert(hda_pack_verb(0, 3, HDA_VERB_SET_PIN_WIDGET_CONTROL, 0x40) ==
	       ((3u << 20) | (0x707u << 8) | 0x40u));
	assert(hda_pack_verb(0, 3, HDA_VERB_SET_EAPD_BTL, 0x02) ==
	       ((3u << 20) | (0x70Cu << 8) | 0x02u));
	assert(hda_pack_verb(0, 3, HDA_VERB_SET_CONN_SELECT, 0x01) ==
	       ((3u << 20) | (0x701u << 8) | 0x01u));
}

/* 3h takes a 16-bit payload: the amp target bits live above bit 7 and
 * must survive.  Truncating to 8 bits would drop OUTPUT/LEFT/RIGHT and
 * leave only the gain, which sets an amp nobody selected. */
static void test_pack_verb_amp_gain_keeps_16bit_payload(void)
{
	uint16_t payload = 0x8000 | 0x2000 | 0x1000 | 0x2A;  /* out, L, R, gain */
	uint32_t v = hda_pack_verb(0, 2, HDA_VERB_SET_AMP_GAIN_MUTE, payload);

	assert(v == ((2u << 20) | (0x300u << 8) | payload));
	assert((v & 0xFFFFu) == payload);
}

/* Ah/Bh are the 16-bit-payload getters; they sit above 0xF00's old
 * threshold in value but must still be short form. */
static void test_pack_verb_getter_ah_is_short_form(void)
{
	uint32_t v = hda_pack_verb(0, 2, HDA_VERB_GET_CONV_FORMAT, 0xBEEF);
	assert(v == ((2u << 20) | (0xA00u << 8) | 0xBEEFu));
}

int main(void) {
	test_pack_verb_0x7xx_is_long_form();
	test_pack_verb_power_pin_eapd_conn_long_form();
	test_pack_verb_amp_gain_keeps_16bit_payload();
	test_pack_verb_getter_ah_is_short_form();
	test_pack_verb_long_form();
	test_pack_verb_long_form_payload_truncated_to_8_bits();
	test_pack_verb_short_form_full_16bit_payload();
	test_pack_verb_codec_address_field();
	test_pack_verb_codec_address_clamped_to_4_bits();
	test_format_every_table_rate_round_trips();
	test_format_rates_the_old_encoder_got_wrong();
	test_format_canonical_rates();
	test_format_8bit_mono_is_zero_but_valid();
	test_format_24bit_marks_correct_bits_field();
	test_format_unsupported_rejected();
	test_bdl_entry_layout_is_packed_16_bytes();
	test_bdl_build_with_and_without_ioc();
	test_bdl_build_null_safe();
	test_conn_short_form_plain();
	test_conn_short_form_spans_responses();
	test_conn_zero_padding_ignored();
	test_conn_short_form_range_expands();
	test_conn_long_form_plain();
	test_conn_long_form_range_and_index();
	test_conn_range_flag_is_not_part_of_the_nid();
	test_conn_backwards_range_is_single_nid();
	test_conn_respects_max();
	puts("host_test_hda: PASS");
	return 0;
}
