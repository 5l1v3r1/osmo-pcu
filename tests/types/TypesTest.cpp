/*
 * TypesTest.cpp Test the primitive data types
 *
 * Copyright (C) 2013 by Holger Hans Peter Freyther
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "bts.h"
#include "tbf.h"
#include "pcu_utils.h"
#include "gprs_debug.h"
#include "encoding.h"
#include "decoding.h"
#include "gprs_rlcmac.h"

extern "C" {
#include <osmocom/core/application.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/bits.h>
}

#define OSMO_ASSERT_STR_EQ(a, b) \
	do { \
		if (strcmp(a, b)) { \
			printf("String mismatch:\nGot:\t%s\nWant:\t%s\n", a, b); \
			OSMO_ASSERT(false); \
		} \
	} while (0)

void *tall_pcu_ctx;
int16_t spoof_mnc = 0, spoof_mcc = 0;
bool spoof_mnc_3_digits = false;

static void test_llc(void)
{
	{
		uint8_t data[LLC_MAX_LEN] = {1, 2, 3, 4, };
		uint8_t out;
		gprs_llc llc;
		llc.init();

		OSMO_ASSERT(llc.chunk_size() == 0);
		OSMO_ASSERT(llc.remaining_space() == LLC_MAX_LEN);
		OSMO_ASSERT(llc.frame_length() == 0);

		llc.put_frame(data, 2);
		OSMO_ASSERT(llc.remaining_space() == LLC_MAX_LEN - 2);
		OSMO_ASSERT(llc.frame_length() == 2);
		OSMO_ASSERT(llc.chunk_size() == 2);
		OSMO_ASSERT(llc.frame[0] == 1);
		OSMO_ASSERT(llc.frame[1] == 2);

		llc.append_frame(&data[3], 1);
		OSMO_ASSERT(llc.remaining_space() == LLC_MAX_LEN - 3);
		OSMO_ASSERT(llc.frame_length() == 3);
		OSMO_ASSERT(llc.chunk_size() == 3);

		/* consume two bytes */
		llc.consume(&out, 1);
		OSMO_ASSERT(llc.remaining_space() == LLC_MAX_LEN - 3);
		OSMO_ASSERT(llc.frame_length() == 3);
		OSMO_ASSERT(llc.chunk_size() == 2);

		/* check that the bytes are as we expected */
		OSMO_ASSERT(llc.frame[0] == 1);
		OSMO_ASSERT(llc.frame[1] == 2);
		OSMO_ASSERT(llc.frame[2] == 4);

		/* now fill the frame */
		llc.append_frame(data, llc.remaining_space() - 1);
		OSMO_ASSERT(llc.fits_in_current_frame(1));
		OSMO_ASSERT(!llc.fits_in_current_frame(2));
	}	
}

static void test_rlc()
{
	{
		struct gprs_rlc_data rlc = { 0, };
		memset(rlc.block, 0x23, RLC_MAX_LEN);
		uint8_t *p = rlc.prepare(20);
		OSMO_ASSERT(p == rlc.block);
		for (int i = 0; i < 20; ++i)
			OSMO_ASSERT(p[i] == 0x2B);
		for (int i = 20; i < RLC_MAX_LEN; ++i)
			OSMO_ASSERT(p[i] == 0x0);
	}
}

static void test_rlc_v_b()
{
	{
		gprs_rlc_v_b vb;
		vb.reset();

		for (size_t i = 0; i < RLC_MAX_SNS; ++i)
			OSMO_ASSERT(vb.is_invalid(i));

		vb.mark_unacked(23);
		OSMO_ASSERT(vb.is_unacked(23));

		vb.mark_nacked(23);
		OSMO_ASSERT(vb.is_nacked(23));

		vb.mark_acked(23);
		OSMO_ASSERT(vb.is_acked(23));

		vb.mark_resend(23);
		OSMO_ASSERT(vb.is_resend(23));

		vb.mark_invalid(23);
		OSMO_ASSERT(vb.is_invalid(23));
	}
}

static void test_rlc_v_n()
{
	{
		gprs_rlc_v_n vn;
		vn.reset();

		OSMO_ASSERT(!vn.is_received(0x23));
		OSMO_ASSERT(vn.state(0x23) == GPRS_RLC_UL_BSN_INVALID);

		vn.mark_received(0x23);
		OSMO_ASSERT(vn.is_received(0x23));
		OSMO_ASSERT(vn.state(0x23) == GPRS_RLC_UL_BSN_RECEIVED);

		vn.mark_missing(0x23);
		OSMO_ASSERT(!vn.is_received(0x23));
		OSMO_ASSERT(vn.state(0x23) == GPRS_RLC_UL_BSN_MISSING);
	}
}

static void test_rlc_dl_ul_basic()
{
	{
		gprs_rlc_dl_window dl_win;
		OSMO_ASSERT(dl_win.window_empty());
		OSMO_ASSERT(!dl_win.window_stalled());
		OSMO_ASSERT(dl_win.distance() == 0);

		dl_win.increment_send();
		OSMO_ASSERT(!dl_win.window_empty());
		OSMO_ASSERT(!dl_win.window_stalled());
		OSMO_ASSERT(dl_win.distance() == 1);

		for (int i = 1; i < 64; ++i) {
			dl_win.increment_send();
			OSMO_ASSERT(!dl_win.window_empty());
			OSMO_ASSERT(dl_win.distance() == i + 1);
		}

		OSMO_ASSERT(dl_win.distance() == 64);
		OSMO_ASSERT(dl_win.window_stalled());

		dl_win.raise(1);
		OSMO_ASSERT(dl_win.distance() == 63);
		OSMO_ASSERT(!dl_win.window_stalled());
		for (int i = 62; i >= 0; --i) {
			dl_win.raise(1);
			OSMO_ASSERT(dl_win.distance() == i);
		}

		OSMO_ASSERT(dl_win.distance() == 0);
		OSMO_ASSERT(dl_win.window_empty());

		dl_win.increment_send();
		dl_win.increment_send();
		dl_win.increment_send();
		dl_win.increment_send();
		OSMO_ASSERT(dl_win.distance() == 4);

		for (int i = 0; i < 128; ++i) {
			dl_win.increment_send();
			dl_win.increment_send();
			dl_win.raise(2);
			OSMO_ASSERT(dl_win.distance() == 4);
		}
	}

	{
		gprs_rlc_ul_window ul_win;
		int count;
		const char *rbb;
		char win_rbb[65];
		uint8_t bin_rbb[8];
		win_rbb[64] = '\0';

		ul_win.m_v_n.reset();

		OSMO_ASSERT(ul_win.is_in_window(0));
		OSMO_ASSERT(ul_win.is_in_window(63));
		OSMO_ASSERT(!ul_win.is_in_window(64));

		OSMO_ASSERT(!ul_win.m_v_n.is_received(0));

		rbb = "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII";
		OSMO_ASSERT(ul_win.ssn() == 0);
		ul_win.update_rbb(win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);
		Encoding::encode_rbb(win_rbb, bin_rbb);
		printf("rbb: %s\n", osmo_hexdump(bin_rbb, sizeof(bin_rbb)));
		Decoding::extract_rbb(bin_rbb, win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);

		/* simulate to have received 0, 1 and 5 */
		OSMO_ASSERT(ul_win.is_in_window(0));
		ul_win.receive_bsn(0);
		count = ul_win.raise_v_q();
		OSMO_ASSERT(ul_win.m_v_n.is_received(0));
		OSMO_ASSERT(ul_win.v_q() == 1);
		OSMO_ASSERT(ul_win.v_r() == 1);
		OSMO_ASSERT(count == 1);

		rbb = "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIR";
		OSMO_ASSERT(ul_win.ssn() == 1);
		ul_win.update_rbb(win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);
		Encoding::encode_rbb(win_rbb, bin_rbb);
		printf("rbb: %s\n", osmo_hexdump(bin_rbb, sizeof(bin_rbb)));
		Decoding::extract_rbb(bin_rbb, win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);

		OSMO_ASSERT(ul_win.is_in_window(1));
		ul_win.receive_bsn(1);
		count = ul_win.raise_v_q();
		OSMO_ASSERT(ul_win.m_v_n.is_received(0));
		OSMO_ASSERT(ul_win.v_q() == 2);
		OSMO_ASSERT(ul_win.v_r() == 2);
		OSMO_ASSERT(count == 1);

		rbb = "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIRR";
		OSMO_ASSERT(ul_win.ssn() == 2);
		ul_win.update_rbb(win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);
		Encoding::encode_rbb(win_rbb, bin_rbb);
		printf("rbb: %s\n", osmo_hexdump(bin_rbb, sizeof(bin_rbb)));
		Decoding::extract_rbb(bin_rbb, win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);

		OSMO_ASSERT(ul_win.is_in_window(5));
		ul_win.receive_bsn(5);
		count = ul_win.raise_v_q();
		OSMO_ASSERT(ul_win.m_v_n.is_received(0));
		OSMO_ASSERT(ul_win.v_q() == 2);
		OSMO_ASSERT(ul_win.v_r() == 6);
		OSMO_ASSERT(count == 0);

		rbb = "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIRRIIIR";
		OSMO_ASSERT(ul_win.ssn() == 6);
		ul_win.update_rbb(win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);
		Encoding::encode_rbb(win_rbb, bin_rbb);
		printf("rbb: %s\n", osmo_hexdump(bin_rbb, sizeof(bin_rbb)));
		Decoding::extract_rbb(bin_rbb, win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);

		OSMO_ASSERT(ul_win.is_in_window(65));
		OSMO_ASSERT(ul_win.is_in_window(2));
		OSMO_ASSERT(ul_win.m_v_n.is_received(5));
		ul_win.receive_bsn(65);
		count = ul_win.raise_v_q();
		OSMO_ASSERT(count == 0);
		OSMO_ASSERT(ul_win.m_v_n.is_received(5));
		OSMO_ASSERT(ul_win.v_q() == 2);
		OSMO_ASSERT(ul_win.v_r() == 66);

		rbb = "IIIRIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIR";
		OSMO_ASSERT(ul_win.ssn() == 66);
		ul_win.update_rbb(win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);
		Encoding::encode_rbb(win_rbb, bin_rbb);
		printf("rbb: %s\n", osmo_hexdump(bin_rbb, sizeof(bin_rbb)));
		Decoding::extract_rbb(bin_rbb, win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);

		OSMO_ASSERT(ul_win.is_in_window(2));
		OSMO_ASSERT(!ul_win.is_in_window(66));
		ul_win.receive_bsn(2);
		count = ul_win.raise_v_q();
		OSMO_ASSERT(count == 1);
		OSMO_ASSERT(ul_win.v_q() == 3);
		OSMO_ASSERT(ul_win.v_r() == 66);

		OSMO_ASSERT(ul_win.is_in_window(66));
		ul_win.receive_bsn(66);
		count = ul_win.raise_v_q();
		OSMO_ASSERT(count == 0);
		OSMO_ASSERT(ul_win.v_q() == 3);
		OSMO_ASSERT(ul_win.v_r() == 67);

		for (int i = 3; i <= 67; ++i) {
			ul_win.receive_bsn(i);
			ul_win.raise_v_q();
		}

		OSMO_ASSERT(ul_win.v_q() == 68);
		OSMO_ASSERT(ul_win.v_r() == 68);

		ul_win.receive_bsn(68);
		count = ul_win.raise_v_q();
		OSMO_ASSERT(ul_win.v_q() == 69);
		OSMO_ASSERT(ul_win.v_r() == 69);
		OSMO_ASSERT(count == 1);

		/* now test the wrapping */
		OSMO_ASSERT(ul_win.is_in_window(4));
		OSMO_ASSERT(!ul_win.is_in_window(5));
		ul_win.receive_bsn(4);
		count = ul_win.raise_v_q();
		OSMO_ASSERT(count == 0);

		/*
		 * SSN wrap around case
		 * Should not expect any BSN as nacked.
		 */
		rbb = "RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR";
		for (int i = 0; i < 128; ++i) {
			ul_win.receive_bsn(i);
			ul_win.raise_v_q();
		}
		ul_win.receive_bsn(0);
		ul_win.raise_v_q();
		ul_win.receive_bsn(1);
		ul_win.raise_v_q();
		ul_win.update_rbb(win_rbb);
		OSMO_ASSERT_STR_EQ(win_rbb, rbb);
		OSMO_ASSERT(ul_win.ssn() == 2);
	}

	{
		uint16_t lost = 0, recv = 0;
		char show_rbb[65];
		uint8_t bits_data[8];
		BTS dummy_bts;
		gprs_rlc_dl_window dl_win;
		bitvec bits;
		int bsn_begin, bsn_end, num_blocks;
		Ack_Nack_Description_t desc;

		dl_win.m_v_b.reset();

		OSMO_ASSERT(dl_win.window_empty());
		OSMO_ASSERT(!dl_win.window_stalled());
		OSMO_ASSERT(dl_win.distance() == 0);

		dl_win.increment_send();
		OSMO_ASSERT(!dl_win.window_empty());
		OSMO_ASSERT(!dl_win.window_stalled());
		OSMO_ASSERT(dl_win.distance() == 1);

		for (int i = 0; i < 35; ++i) {
			dl_win.increment_send();
			OSMO_ASSERT(!dl_win.window_empty());
			OSMO_ASSERT(dl_win.distance() == i + 2);
		}

		uint8_t rbb_cmp[8] = { 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xff };
		bits.data = bits_data;
		bits.data_len = sizeof(bits_data);
		bits.cur_bit = 0;

		memcpy(desc.RECEIVED_BLOCK_BITMAP, rbb_cmp,
			sizeof(desc.RECEIVED_BLOCK_BITMAP));
		desc.FINAL_ACK_INDICATION = 0;
		desc.STARTING_SEQUENCE_NUMBER = 35;

		num_blocks = Decoding::decode_gprs_acknack_bits(
			&desc, &bits,
			&bsn_begin, &bsn_end, &dl_win);
		Decoding::extract_rbb(&bits, show_rbb);
		printf("show_rbb: %s\n", show_rbb);

		dl_win.update(&dummy_bts, &bits, 0, &lost, &recv);
		OSMO_ASSERT(lost == 0);
		OSMO_ASSERT(recv == 35);
		OSMO_ASSERT(bsn_begin == 0);
		OSMO_ASSERT(bsn_end == 35);
		OSMO_ASSERT(num_blocks == 35);

		dl_win.raise(dl_win.move_window());

		for (int i = 0; i < 8; ++i) {
			dl_win.increment_send();
			OSMO_ASSERT(!dl_win.window_empty());
			OSMO_ASSERT(dl_win.distance() == 2 + i);
		}

		uint8_t rbb_cmp2[8] = { 0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xff, 0x31 };
		bits.data = bits_data;
		bits.data_len = sizeof(bits_data);
		bits.cur_bit = 0;

		memcpy(desc.RECEIVED_BLOCK_BITMAP, rbb_cmp2,
			sizeof(desc.RECEIVED_BLOCK_BITMAP));
		desc.FINAL_ACK_INDICATION = 0;
		desc.STARTING_SEQUENCE_NUMBER = 35 + 8;

		num_blocks = Decoding::decode_gprs_acknack_bits(
			&desc, &bits,
			&bsn_begin, &bsn_end, &dl_win);
		Decoding::extract_rbb(&bits, show_rbb);
		printf("show_rbb: %s\n", show_rbb);

		lost = recv = 0;
		dl_win.update(&dummy_bts, &bits, 0, &lost, &recv);
		OSMO_ASSERT(lost == 5);
		OSMO_ASSERT(recv == 3);
		OSMO_ASSERT(bitvec_get_bit_pos(&bits, 0) == 0);
		OSMO_ASSERT(bitvec_get_bit_pos(&bits, 7) == 1);
		OSMO_ASSERT(bsn_begin == 35);
		OSMO_ASSERT(bsn_end == 43);
		OSMO_ASSERT(num_blocks == 8);
	}
}

static void check_imm_ass(struct gprs_rlcmac_tbf *tbf, bool dl, enum ph_burst_type bt, const uint8_t *exp, uint8_t len,
			  const char *kind)
{
	uint8_t alpha = 7, gamma = 8, ta = 35, ts = 5, tsc = 1, usf = 1, sz = sizeof(DUMMY_VEC) / 2, plen;
	bitvec *immediate_assignment = bitvec_alloc(sz, tall_pcu_ctx);
	struct msgb *m = msgb_alloc(80, "test");
	bool poll = true;
	uint16_t ra = 13, arfcn = 877;
	uint32_t ref_fn = 24, fn = 11;
	int8_t ta_idx = 0;

	bitvec_unhex(immediate_assignment, DUMMY_VEC);
	plen = Encoding::write_immediate_assignment(tbf, immediate_assignment, dl,
						    ra, ref_fn, ta, arfcn, ts, tsc, usf,
						    poll, fn, alpha, gamma, ta_idx, bt);
	printf("[%u] %s Immediate Assignment <%s>:\n\t%s\n", plen, dl ? "DL" : "UL", kind,
	       osmo_hexdump(immediate_assignment->data, sz));

	memcpy(msgb_put(m, sz), immediate_assignment->data, sz);
	if (!msgb_eq_data_print(m, exp, len))
		printf("%s(%s, %s) failed!\n", __func__, dl ? "DL" : "UL", kind);

	msgb_free(m);
}

void test_immediate_assign_dl()
{
	BTS the_bts;
	the_bts.bts_data()->alloc_algorithm = alloc_algorithm_a;
	the_bts.bts_data()->trx[0].pdch[2].enable();
	the_bts.bts_data()->trx[0].pdch[3].enable();

	struct gprs_rlcmac_tbf *tbf = tbf_alloc_dl_tbf(the_bts.bts_data(), NULL, 0, 1, 1, false);
	static uint8_t res[] = { 0x06,
				 0x3f, /* Immediate Assignment Message Type */
				 0x30, /* §10.5.2.26 Page Mode and §10.5.2.25b Dedicated mode/TBF */
				 0x0d, 0x23, 0x6d, /* §10.5.2.25a Packet Channel Description */
				 /* ETSI TS 44.018 §10.5.2.30 Request Reference */
				 0x7f, /* RA */
				 0x03, 0x18, /* T1'-T3 */
				 0x23, /* TA */
				 0x00, /* 0-length §10.5.2.21 Mobile Allocation */
				 /* ETSI TS 44.018 §10.5.2.16 IA Rest Octets */
				 0xd0, 0x00, 0x00, 0x00, 0x08, 0x17, 0x47, 0x08, 0x0b, 0x5b, 0x2b, 0x2b, };

	check_imm_ass(tbf, true, GSM_L1_BURST_TYPE_ACCESS_2, res, sizeof(res), "ia_rest_downlink");
}

void test_immediate_assign_ul0m()
{
	BTS the_bts;
	the_bts.bts_data()->alloc_algorithm = alloc_algorithm_a;
	the_bts.bts_data()->trx[0].pdch[4].enable();
	the_bts.bts_data()->trx[0].pdch[5].enable();

	struct gprs_rlcmac_tbf *tbf = tbf_alloc_ul_tbf(the_bts.bts_data(), NULL, 0, 1, 1, false);
	static uint8_t res[] = { 0x06,
				 0x3f, /* Immediate Assignment Message Type */
				 0x10, /* §10.5.2.26 Page Mode and §10.5.2.25b Dedicated mode/TBF */
				 0x0d, 0x23, 0x6d, /* §10.5.2.25a Packet Channel Description */
				 /* ETSI TS 44.018 §10.5.2.30 Request Reference */
				 0x0d, /* RA */
				 0x03, 0x18, /* T1'-T3 */
				 0x23, /* TA */
				 0x00, /* 0-length §10.5.2.21 Mobile Allocation */
				 /* ETSI TS 44.018 §10.5.2.16 IA Rest Octets */
				 0xc8, 0x02, 0x1b, 0xa2, 0x0b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, };

	check_imm_ass(tbf, false, GSM_L1_BURST_TYPE_ACCESS_0, res, sizeof(res), "ia_rest_uplink(MBA)");
}

void test_immediate_assign_ul0s()
{
	static uint8_t res[] = { 0x06,
				 0x3f, /* Immediate Assignment Message Type */
				 0x10, /* §10.5.2.26 Page Mode and §10.5.2.25b Dedicated mode/TBF */
				 0x0d, 0x23, 0x6d, /* §10.5.2.25a Packet Channel Description */
				 /* ETSI TS 44.018 §10.5.2.30 Request Reference */
				 0x0d, /* RA */
				 0x03, 0x18, /* T1'-T3 */
				 0x23, /* TA */
				 0x00, /* 0-length §10.5.2.21 Mobile Allocation */
				 /* ETSI TS 44.018 §10.5.2.16 IA Rest Octets */
				 0xc5, 0xd0, 0x80, 0xb5, 0xab, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, };

	check_imm_ass(NULL, false, GSM_L1_BURST_TYPE_ACCESS_0, res, sizeof(res), "ia_rest_uplink(SBA)");
}

void test_immediate_assign_ul1s()
{
	BTS the_bts;
	the_bts.bts_data()->alloc_algorithm = alloc_algorithm_a;
	the_bts.bts_data()->trx[0].pdch[1].enable();
	the_bts.bts_data()->trx[0].pdch[2].enable();

	struct gprs_rlcmac_tbf *tbf = tbf_alloc_ul_tbf(the_bts.bts_data(), NULL, 0, 1, 1, false);
	static uint8_t res[] = { 0x06,
				 0x3f, /* Immediate Assignment Message Type */
				 0x10, /* §10.5.2.26 Page Mode and §10.5.2.25b Dedicated mode/TBF */
				 0x0d, 0x23, 0x6d, /* §10.5.2.25a Packet Channel Description */
				 /* ETSI TS 44.018 §10.5.2.30 Request Reference */
				 0x7f, /* RA */
				 0x03, 0x18, /* T1'-T3 */
				 0x23, /* TA */
				 0x00, /* 0-length §10.5.2.21 Mobile Allocation */
				 /* ETSI TS 44.018 §10.5.2.16 IA Rest Octets */
				 0x46, 0xa0, 0x08, 0x00, 0x17, 0x44, 0x0b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, };

	check_imm_ass(tbf, false, GSM_L1_BURST_TYPE_ACCESS_1, res, sizeof(res), "ia_rest_egprs_uplink(SBA)");
}

void test_immediate_assign_ul1m()
{
	static uint8_t res[] = { 0x06,
				 0x3f, /* Immediate Assignment Message Type */
				 0x10, /* §10.5.2.26 Page Mode and §10.5.2.25b Dedicated mode/TBF */
				 0x0d, 0x23, 0x6d, /* §10.5.2.25a Packet Channel Description */
				 /* ETSI TS 44.018 §10.5.2.30 Request Reference */
				 0x7f, /* RA */
				 0x03, 0x18, /* T1'-T3 */
				 0x23, /* TA */
				 0x00, /* 0-length §10.5.2.21 Mobile Allocation */
				 /* ETSI TS 44.018 §10.5.2.16 IA Rest Octets */
				 0x46, 0x97, 0x40, 0x0b, 0x58, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, };

	check_imm_ass(NULL, false, GSM_L1_BURST_TYPE_ACCESS_1, res, sizeof(res), "ia_rest_egprs_uplink(MBA)");
}

void test_immediate_assign_rej()
{
	uint8_t plen;
	bitvec *immediate_assignment_rej = bitvec_alloc(22, tall_pcu_ctx);

	bitvec_unhex(immediate_assignment_rej, DUMMY_VEC);
	plen = Encoding::write_immediate_assignment_reject(
		immediate_assignment_rej, 112, 100,
		GSM_L1_BURST_TYPE_ACCESS_1);

	printf("assignment reject: %s\n",
		osmo_hexdump(immediate_assignment_rej->data, 22));

	OSMO_ASSERT(plen == 19);
	/* RA value */
	OSMO_ASSERT(immediate_assignment_rej->data[3] == 0x7f);
	/* Extended RA value */
	OSMO_ASSERT(immediate_assignment_rej->data[19] == 0xc0);

	bitvec_unhex(immediate_assignment_rej, DUMMY_VEC);

	plen = Encoding::write_immediate_assignment_reject(
		immediate_assignment_rej, 112, 100,
		GSM_L1_BURST_TYPE_ACCESS_0);

	printf("assignment reject: %s\n",
		osmo_hexdump(immediate_assignment_rej->data, 22));

	OSMO_ASSERT(plen == 19);
	/* RA value */
	OSMO_ASSERT(immediate_assignment_rej->data[3] == 0x70);

}

static void test_lsb()
{
	uint8_t u = 0;

	printf("Testing LBS utility...\n");

	do {
		uint8_t x = pcu_lsb(u); /* equivalent of (1 << ffs(u)) / 2 */
		printf("%2X " OSMO_BIT_SPEC ": {%d} %3d\n",
		       u, OSMO_BIT_PRINT(u), pcu_bitcount(u), x);
		u++;
	} while (u);
}

int main(int argc, char **argv)
{
	tall_pcu_ctx = talloc_named_const(NULL, 1, "types test context");
	if (!tall_pcu_ctx)
		abort();

	msgb_talloc_ctx_init(tall_pcu_ctx, 0);
	osmo_init_logging2(tall_pcu_ctx, &gprs_log_info);
	log_set_use_color(osmo_stderr_target, 0);
	log_set_print_filename(osmo_stderr_target, 0);

	printf("Making some basic type testing.\n");
	test_llc();
	test_rlc();
	test_rlc_v_b();
	test_rlc_v_n();
	test_rlc_dl_ul_basic();
	test_immediate_assign_dl();
	test_immediate_assign_ul0m();
	test_immediate_assign_ul0s();
	test_immediate_assign_ul1m();
	test_immediate_assign_ul1s();
	test_immediate_assign_rej();
	test_lsb();

	return EXIT_SUCCESS;
}

/*
 * stubs that should not be reached
 */
extern "C" {
void l1if_pdch_req() { abort(); }
void l1if_connect_pdch() { abort(); }
void l1if_close_pdch() { abort(); }
void l1if_open_pdch() { abort(); }
}
