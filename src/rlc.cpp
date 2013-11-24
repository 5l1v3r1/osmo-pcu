/*
 * Copyright (C) 2013 by Holger Hans Peter Freyther
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "tbf.h"
#include "bts.h"
#include "gprs_debug.h"

extern "C" {
#include <osmocom/core/utils.h>
}


uint8_t *gprs_rlc_data::prepare(size_t block_data_len)
{
	/* todo.. only set it once if it turns out to be a bottleneck */
	memset(block, 0x0, ARRAY_SIZE(block));
	memset(block, 0x2b, block_data_len);

	return block;
}

void gprs_rlc_v_b::reset()
{
	for (size_t i = 0; i < ARRAY_SIZE(m_v_b); ++i)
		mark_invalid(i);
}

int gprs_rlc_v_b::resend_needed(const uint16_t v_a, const uint16_t v_s,
				const uint16_t mod_sns,
				const uint16_t mod_sns_half)
{
	for (uint16_t bsn = v_a; bsn != v_s; bsn = (bsn + 1) & mod_sns) {
		uint16_t index = bsn & mod_sns_half;
		if (is_nacked(index) || is_resend(index))
			return bsn;
	}

	return -1;
}

int gprs_rlc_v_b::mark_for_resend(const uint16_t v_a, const uint16_t v_s,
				const uint16_t mod_sns,
				const uint16_t mod_sns_half)
{
	int resend = 0;

	for (uint16_t bsn = v_a; bsn != v_s; bsn = (bsn + 1) & mod_sns) {
		uint16_t index = (bsn & mod_sns_half);
		if (is_unacked(index)) {
			/* mark to be re-send */
			mark_resend(index);
			resend += 1;
		}
	}

	return resend;
}

int gprs_rlc_v_b::count_unacked(const uint16_t v_a, const uint16_t v_s,
			const uint16_t mod_sns, const uint16_t mod_sns_half)
{
	uint16_t unacked = 0;
	uint16_t bsn;

	for (bsn = v_a; bsn != v_s; bsn = (bsn + 1) & mod_sns) {
		uint16_t index = bsn & mod_sns_half;
		if (!is_acked(index))
			unacked += 1;
	}

	return unacked;
}

void gprs_rlc_v_b::update(BTS *bts, char *show_rbb, uint8_t ssn,
			const uint16_t v_a,
			const uint16_t mod_sns, const uint16_t mod_sns_half,
			uint16_t *lost, uint16_t *received)
{
	uint16_t bsn;
	int i;

	/* SSN - 1 is in range V(A)..V(S)-1 */
	for (i = 63, bsn = (ssn - 1) & mod_sns;
	     i >= 0 && bsn != ((v_a - 1) & mod_sns);
	     i--, bsn = (bsn - 1) & mod_sns) {

		if (show_rbb[i] == '1') {
			LOGP(DRLCMACDL, LOGL_DEBUG, "- got ack for BSN=%d\n", bsn);
			if (!is_acked(bsn & mod_sns_half))
				*received += 1;
			mark_acked(bsn & mod_sns_half);
		} else {
			LOGP(DRLCMACDL, LOGL_DEBUG, "- got NACK for BSN=%d\n", bsn);
			mark_nacked(bsn & mod_sns_half);
			bts->rlc_nacked();
			*lost += 1;
		}
	}
}

int gprs_rlc_v_b::move_window(const uint16_t v_a, const uint16_t v_s,
				const uint16_t mod_sns, const uint16_t mod_sns_half)
{
	int i;
	uint16_t bsn;
	int moved = 0;

	for (i = 0, bsn = v_a; bsn != v_s; i++, bsn = (bsn + 1) & mod_sns) {
		uint16_t index = (bsn & mod_sns_half);
		if (is_acked(index)) {
			mark_invalid(index);
			moved += 1;
		} else
			break;
	}

	return moved;
}

void gprs_rlc_v_b::state(char *show_v_b, const uint16_t v_a, const uint16_t v_s,
			const uint16_t mod_sns, const uint16_t mod_sns_half)
{
	int i;
	uint16_t bsn;

	for (i = 0, bsn = v_a; bsn != v_s; i++, bsn = (bsn + 1) & mod_sns) {
		uint16_t index = (bsn & mod_sns_half);
		show_v_b[i] = m_v_b[index];
		if (show_v_b[i] == 0)
			show_v_b[i] = ' ';
	}
	show_v_b[i] = '\0';
}
