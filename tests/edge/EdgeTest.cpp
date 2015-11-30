/*
 * EdgeTest.cpp
 *
 * Copyright (C) 2015 by Sysmocom s.f.m.c. GmbH
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

#include "gprs_debug.h"
#include "gprs_coding_scheme.h"

extern "C" {
#include "pcu_vty.h"

#include <osmocom/core/application.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>
#include <osmocom/vty/vty.h>
}

#include <errno.h>

void *tall_pcu_ctx;
int16_t spoof_mnc = 0, spoof_mcc = 0;

static void check_coding_scheme(GprsCodingScheme& cs, GprsCodingScheme::Mode mode)
{
	volatile unsigned expected_size;
	GprsCodingScheme new_cs;

	OSMO_ASSERT(cs.isValid());
	OSMO_ASSERT(cs.isCompatible(mode));

	/* Check static getBySizeUL() */
	expected_size = cs.maxBytesUL();
	if (cs.spareBitsUL() > 0)
		expected_size += 1;
	OSMO_ASSERT(expected_size == cs.sizeUL());
	OSMO_ASSERT(cs == GprsCodingScheme::getBySizeUL(expected_size));

	/* Check static sizeUL() */
	expected_size = cs.maxBytesDL();
	if (cs.spareBitsDL() > 0)
		expected_size += 1;
	OSMO_ASSERT(expected_size == cs.sizeDL());

	/* Check inc/dec */
	new_cs = cs;
	new_cs.inc(mode);
	OSMO_ASSERT(new_cs.isCompatible(mode));
	if (new_cs != cs) {
		new_cs.dec(mode);
		OSMO_ASSERT(new_cs.isCompatible(mode));
		OSMO_ASSERT(new_cs == cs);
	}
	new_cs.dec(mode);
	OSMO_ASSERT(new_cs.isCompatible(mode));
	if (new_cs != cs) {
		new_cs.inc(mode);
		OSMO_ASSERT(new_cs.isCompatible(mode));
		OSMO_ASSERT(new_cs == cs);
	}
}

static void test_coding_scheme()
{
	unsigned i;
	unsigned last_size_UL;
	unsigned last_size_DL;
	GprsCodingScheme::Scheme gprs_schemes[] = {
		GprsCodingScheme::CS1,
		GprsCodingScheme::CS2,
		GprsCodingScheme::CS3,
		GprsCodingScheme::CS4
	};
	struct {
		GprsCodingScheme::Scheme s;
		bool is_gmsk;
	} egprs_schemes[] = {
		{GprsCodingScheme::MCS1, true},
		{GprsCodingScheme::MCS2, true},
		{GprsCodingScheme::MCS3, true},
		{GprsCodingScheme::MCS4, true},
		{GprsCodingScheme::MCS5, false},
		{GprsCodingScheme::MCS6, false},
		{GprsCodingScheme::MCS7, false},
		{GprsCodingScheme::MCS8, false},
		{GprsCodingScheme::MCS9, false},
	};

	printf("=== start %s ===\n", __func__);

	GprsCodingScheme cs;
	OSMO_ASSERT(!cs);
	OSMO_ASSERT(cs == GprsCodingScheme::UNKNOWN);
	OSMO_ASSERT(!cs.isCompatible(GprsCodingScheme::GPRS));
	OSMO_ASSERT(!cs.isCompatible(GprsCodingScheme::EGPRS_GMSK));
	OSMO_ASSERT(!cs.isCompatible(GprsCodingScheme::EGPRS));

	last_size_UL = 0;
	last_size_DL = 0;

	for (i = 0; i < ARRAY_SIZE(gprs_schemes); i++) {
		GprsCodingScheme current_cs(gprs_schemes[i]);
		OSMO_ASSERT(current_cs.isGprs());
		OSMO_ASSERT(!current_cs.isEgprs());
		OSMO_ASSERT(!current_cs.isEgprsGmsk());
		OSMO_ASSERT(current_cs == gprs_schemes[i]);
		OSMO_ASSERT(current_cs == GprsCodingScheme(gprs_schemes[i]));

		/* Check strong monotonicity */
		OSMO_ASSERT(current_cs.maxBytesUL() > last_size_UL);
		OSMO_ASSERT(current_cs.maxBytesDL() > last_size_DL);
		last_size_UL = current_cs.maxBytesUL();
		last_size_DL = current_cs.maxBytesDL();

		check_coding_scheme(current_cs, GprsCodingScheme::GPRS);
	}
	OSMO_ASSERT(i == 4);

	last_size_UL = 0;
	last_size_DL = 0;

	for (i = 0; i < ARRAY_SIZE(egprs_schemes); i++) {
		GprsCodingScheme current_cs(egprs_schemes[i].s);
		OSMO_ASSERT(!current_cs.isGprs());
		OSMO_ASSERT(current_cs.isEgprs());
		OSMO_ASSERT(!!current_cs.isEgprsGmsk() == !!egprs_schemes[i].is_gmsk);
		OSMO_ASSERT(current_cs == egprs_schemes[i].s);
		OSMO_ASSERT(current_cs == GprsCodingScheme(egprs_schemes[i].s));

		/* Check strong monotonicity */
		OSMO_ASSERT(current_cs.maxBytesUL() > last_size_UL);
		OSMO_ASSERT(current_cs.maxBytesDL() > last_size_DL);
		last_size_UL = current_cs.maxBytesUL();
		last_size_DL = current_cs.maxBytesDL();

		if (egprs_schemes[i].is_gmsk)
			check_coding_scheme(current_cs, GprsCodingScheme::EGPRS_GMSK);
		check_coding_scheme(current_cs, GprsCodingScheme::EGPRS);
	}
	OSMO_ASSERT(i == 9);

	printf("=== end %s ===\n", __func__);
}


static const struct log_info_cat default_categories[] = {
	{"DCSN1", "\033[1;31m", "Concrete Syntax Notation One (CSN1)", LOGL_INFO, 0},
	{"DL1IF", "\033[1;32m", "GPRS PCU L1 interface (L1IF)", LOGL_DEBUG, 1},
	{"DRLCMAC", "\033[0;33m", "GPRS RLC/MAC layer (RLCMAC)", LOGL_DEBUG, 1},
	{"DRLCMACDATA", "\033[0;33m", "GPRS RLC/MAC layer Data (RLCMAC)", LOGL_DEBUG, 1},
	{"DRLCMACDL", "\033[1;33m", "GPRS RLC/MAC layer Downlink (RLCMAC)", LOGL_DEBUG, 1},
	{"DRLCMACUL", "\033[1;36m", "GPRS RLC/MAC layer Uplink (RLCMAC)", LOGL_DEBUG, 1},
	{"DRLCMACSCHED", "\033[0;36m", "GPRS RLC/MAC layer Scheduling (RLCMAC)", LOGL_DEBUG, 1},
	{"DRLCMACMEAS", "\033[1;31m", "GPRS RLC/MAC layer Measurements (RLCMAC)", LOGL_INFO, 1},
	{"DNS","\033[1;34m", "GPRS Network Service Protocol (NS)", LOGL_INFO , 1},
	{"DBSSGP","\033[1;34m", "GPRS BSS Gateway Protocol (BSSGP)", LOGL_INFO , 1},
	{"DPCU", "\033[1;35m", "GPRS Packet Control Unit (PCU)", LOGL_NOTICE, 1},
};

static int filter_fn(const struct log_context *ctx,
	struct log_target *tar)
{
	return 1;
}

const struct log_info debug_log_info = {
	filter_fn,
	(struct log_info_cat*)default_categories,
	ARRAY_SIZE(default_categories),
};

int main(int argc, char **argv)
{
	struct vty_app_info pcu_vty_info = {0};

	tall_pcu_ctx = talloc_named_const(NULL, 1, "EdgeTest context");
	if (!tall_pcu_ctx)
		abort();

	msgb_set_talloc_ctx(tall_pcu_ctx);
	osmo_init_logging(&debug_log_info);
	log_set_use_color(osmo_stderr_target, 0);
	log_set_print_filename(osmo_stderr_target, 0);

	vty_init(&pcu_vty_info);
	pcu_vty_init(&debug_log_info);

	test_coding_scheme();

	if (getenv("TALLOC_REPORT_FULL"))
		talloc_report_full(tall_pcu_ctx, stderr);
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