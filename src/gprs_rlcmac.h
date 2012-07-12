/* gprs_rlcmac.h
 *
 * Copyright (C) 2012 Ivan Klyuchnikov
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
 
#ifndef GPRS_RLCMAC_H
#define GPRS_RLCMAC_H

#include <bitvector.h>
#include <gsm_rlcmac.h>
#include <gsm_timer.h>

extern "C" {
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/timer.h>
}

/* This special feature will delay assignment of downlink TBF by one second,
 * in case there is already a TBF.
 * This is usefull to debug downlink establishment during packet idle mode.
 */
//#define DEBUG_DL_ASS_IDLE

/*
 * PDCH instanc
 */

struct gprs_rlcmac_tbf;

struct gprs_rlcmac_pdch {
	uint8_t enable; /* TS is enabled */
	uint8_t tsc; /* TSC of this slot */
	uint8_t next_ul_tfi; /* next uplink TBF/TFI to schedule (0..31) */
	uint8_t next_dl_tfi; /* next downlink TBF/TFI to schedule (0..31) */
	struct gprs_rlcmac_tbf *tbf[32]; /* array of TBF pointers, by TFI */
	uint32_t last_rts_fn; /* store last frame number of RTS */
};

struct gprs_rlcmac_trx {
	uint16_t arfcn;
	struct gprs_rlcmac_pdch pdch[8];
};

struct gprs_rlcmac_bts {
	uint8_t cs1;
	uint8_t cs2;
	uint8_t cs3;
	uint8_t cs4;
	uint8_t initial_cs;
	uint8_t t3142;
	uint8_t t3169;
	uint8_t t3191;
	uint16_t t3193_msec;
	uint8_t t3195;
	uint8_t n3101;
	uint8_t n3103;
	uint8_t n3105;
	struct gprs_rlcmac_trx trx[8];
};

extern struct gprs_rlcmac_bts *gprs_rlcmac_bts;

/*
 * TBF instance
 */

#define LLC_MAX_LEN 1543
#define RLC_MAX_SNS 128 /* GPRS, must be power of 2 */
#define RLC_MAX_WS  64 /* max window size */
#define RLC_MAX_LEN 54 /* CS-4 including spare bits */

#define Tassign_agch 0,500000/* wait for assignment, before transmitting DL */
#define Tassign_pacch 0,100000/* wait for assignment, before transmitting DL */

enum gprs_rlcmac_tbf_state {
	GPRS_RLCMAC_NULL = 0,	/* new created TBF */
	GPRS_RLCMAC_ASSIGN,	/* wait for downlink assignment */
	GPRS_RLCMAC_FLOW,	/* RLC/MAC flow, ressource needed */
	GPRS_RLCMAC_FINISHED,	/* flow finished, wait for release */
	GPRS_RLCMAC_WAIT_RELEASE,/* wait for release or restart of DL TBF */
	GPRS_RLCMAC_RELEASING,	/* releasing, wait to free TBI/USF */
};

enum gprs_rlcmac_tbf_poll_state {
	GPRS_RLCMAC_POLL_NONE = 0,
	GPRS_RLCMAC_POLL_SCHED, /* a polling was scheduled */
};

enum gprs_rlcmac_tbf_dl_ass_state {
	GPRS_RLCMAC_DL_ASS_NONE = 0,
	GPRS_RLCMAC_DL_ASS_SEND_ASS, /* send downlink assignment on next RTS */
	GPRS_RLCMAC_DL_ASS_WAIT_ACK, /* wait for PACKET CONTROL ACK */
};

enum gprs_rlcmac_tbf_ul_ass_state {
	GPRS_RLCMAC_UL_ASS_NONE = 0,
	GPRS_RLCMAC_UL_ASS_SEND_ASS, /* send uplink assignment on next RTS */
	GPRS_RLCMAC_UL_ASS_WAIT_ACK, /* wait for PACKET CONTROL ACK */
};

enum gprs_rlcmac_tbf_ul_ack_state {
	GPRS_RLCMAC_UL_ACK_NONE = 0,
	GPRS_RLCMAC_UL_ACK_SEND_ACK, /* send acknowledge on next RTS */
	GPRS_RLCMAC_UL_ACK_WAIT_ACK, /* wait for PACKET CONTROL ACK */
};

enum gprs_rlcmac_tbf_direction {
	GPRS_RLCMAC_DL_TBF,
	GPRS_RLCMAC_UL_TBF
};

struct gprs_rlcmac_tbf {
	struct llist_head list;
	enum gprs_rlcmac_tbf_state state;
	enum gprs_rlcmac_tbf_direction direction;
	uint8_t tfi;
	uint32_t tlli;
	uint8_t tlli_valid;
	uint8_t trx, ts, tsc;
	struct gprs_rlcmac_pdch *pdch;
	uint16_t arfcn, ta;
	uint8_t llc_frame[LLC_MAX_LEN]; /* current DL or UL frame */
	uint16_t llc_index; /* current write/read position of frame */
	uint16_t llc_length; /* len of current DL LLC_frame, 0 == no frame */
	llist_head llc_queue; /* queued LLC DL data */

	enum gprs_rlcmac_tbf_dl_ass_state dl_ass_state;
	enum gprs_rlcmac_tbf_ul_ass_state ul_ass_state;
	enum gprs_rlcmac_tbf_ul_ack_state ul_ack_state;

	enum gprs_rlcmac_tbf_poll_state poll_state;
	uint32_t poll_fn;

	uint16_t ws;	/* window size */
	uint16_t sns;	/* sequence number space */

	/* Please note that all variables here will be reset when changing
	 * from WAIT RELEASE back to FLOW state (re-use of TBF).
	 * All states that need reset must be in this struct, so this is why
	 * variables are in both (dl and ul) structs and not outside union.
	 */
	union {
		struct {
			uint16_t bsn;	/* block sequence number */
			uint16_t v_s;	/* send state */
			uint16_t v_a;	/* ack state */
			char v_b[RLC_MAX_SNS/2]; /* acknowledge state array */
			int32_t tx_counter; /* count all transmitted blocks */
			uint8_t n3105;	/* N3105 counter */
		} dl;
		struct {
			uint16_t bsn;	/* block sequence number */
			uint16_t v_r;	/* receive state */
			uint16_t v_q;	/* receive window state */
			char v_n[RLC_MAX_SNS/2]; /* receive state array */
			int32_t rx_counter; /* count all received blocks */
			uint8_t n3103;	/* N3103 counter */
			uint8_t usf;	/* USF */
		} ul;
	} dir;
	uint8_t rlc_block[RLC_MAX_SNS/2][RLC_MAX_LEN]; /* block history */
	uint8_t rlc_block_len[RLC_MAX_SNS/2]; /* block len  of history */
	
	struct osmo_timer_list	timer;
	unsigned int T; /* Txxxx number */
	unsigned int num_T_exp; /* number of consecutive T expirations */
	
	struct osmo_gsm_timer_list	gsm_timer;
	unsigned int fT; /* fTxxxx number */
	unsigned int num_fT_exp; /* number of consecutive fT expirations */
};

extern struct llist_head gprs_rlcmac_tbfs;

int tfi_alloc(uint8_t *_trx, uint8_t *_ts);

struct gprs_rlcmac_tbf *tbf_alloc(uint8_t tfi, uint8_t trx, uint8_t ts);

struct gprs_rlcmac_tbf *tbf_by_tfi(uint8_t tfi, int direction);

struct gprs_rlcmac_tbf *tbf_by_tlli(uint32_t tlli, int direction);

struct gprs_rlcmac_tbf *tbf_by_poll_fn(uint32_t fn);

int find_free_usf(uint8_t trx, uint8_t ts);

void tbf_free(struct gprs_rlcmac_tbf *tbf);

void tbf_new_state(struct gprs_rlcmac_tbf *tbf,
        enum gprs_rlcmac_tbf_state state);

void tbf_timer_start(struct gprs_rlcmac_tbf *tbf, unsigned int T,
                        unsigned int seconds, unsigned int microseconds);

void tbf_timer_stop(struct gprs_rlcmac_tbf *tbf);

/* TS 44.060 Section 10.4.7 Table 10.4.7.1: Payload Type field */
enum gprs_rlcmac_block_type {
	GPRS_RLCMAC_DATA_BLOCK = 0x0,
	GPRS_RLCMAC_CONTROL_BLOCK = 0x1, 
	GPRS_RLCMAC_CONTROL_BLOCK_OPT = 0x2,
	GPRS_RLCMAC_RESERVED = 0x3
};

int gprs_rlcmac_rcv_block(uint8_t *data, uint8_t len, uint32_t fn);

int write_immediate_assignment(bitvec * dest, uint8_t downlink, uint8_t ra, 
        uint32_t fn, uint8_t ta, uint16_t arfcn, uint8_t ts, uint8_t tsc, 
        uint8_t tfi, uint8_t usf, uint32_t tlli, uint8_t polling,
	uint32_t poll_fn);

void write_packet_uplink_assignment(bitvec * dest, uint8_t old_tfi,
        uint8_t old_downlink, uint32_t tlli, uint8_t use_tlli, uint8_t new_tfi,
        uint8_t usf, uint16_t arfcn, uint8_t tn, uint8_t ta, uint8_t tsc,
        uint8_t poll);

void write_packet_downlink_assignment(RlcMacDownlink_t * block, uint8_t old_tfi,
        uint8_t old_downlink, uint8_t new_tfi, uint16_t arfcn,
        uint8_t tn, uint8_t ta, uint8_t tsc, uint8_t poll);

void write_packet_uplink_ack(RlcMacDownlink_t * block, struct gprs_rlcmac_tbf *tbf,
        uint8_t final);

int gprs_rlcmac_tx_ul_ud(gprs_rlcmac_tbf *tbf);

void tbf_timer_cb(void *_tbf);

int gprs_rlcmac_poll_timeout(struct gprs_rlcmac_tbf *tbf);

int gprs_rlcmac_rcv_rach(uint8_t ra, uint32_t Fn, int16_t qta);

int gprs_rlcmac_rcv_control_block(bitvec *rlc_block, uint32_t fn);

struct msgb *gprs_rlcmac_send_packet_uplink_assignment(
        struct gprs_rlcmac_tbf *tbf, uint32_t fn);

struct msgb *gprs_rlcmac_send_packet_downlink_assignment(
        struct gprs_rlcmac_tbf *tbf, uint32_t fn);

void gprs_rlcmac_trigger_downlink_assignment(gprs_rlcmac_tbf *tbf,
	uint8_t old_downlink, char *imsi);

int gprs_rlcmac_downlink_ack(struct gprs_rlcmac_tbf *tbf, uint8_t final,
        uint8_t ssn, uint8_t *rbb);

int gprs_rlcmac_rcv_data_block_acknowledged(uint8_t *data, uint8_t len);

struct msgb *gprs_rlcmac_send_data_block_acknowledged(
        struct gprs_rlcmac_tbf *tbf, uint32_t fn);

struct msgb *gprs_rlcmac_send_uplink_ack(struct gprs_rlcmac_tbf *tbf,
        uint32_t fn);

int gprs_rlcmac_rcv_rts_block(uint8_t trx, uint8_t ts, uint16_t arfcn, 
        uint32_t fn, uint8_t block_nr);

#endif // GPRS_RLCMAC_H