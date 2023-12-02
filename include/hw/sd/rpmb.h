// SPDX-License-Identifier: GPL-2.0+
/*
 * eMMC- Replay Protected Memory Block
 * According to JEDEC Standard No. 84-A441
 */

#ifndef HW_RPMB_H
#define HW_RPMB_H

#include "qemu/osdep.h"
#include "hw/sd/sd.h"

/* Request codes */
#define RPMB_REQ_KEY		1
#define RPMB_REQ_WCOUNTER	2
#define RPMB_REQ_WRITE_DATA	3
#define RPMB_REQ_READ_DATA	4
#define RPMB_REQ_STATUS		5

/* Response code */
#define RPMB_RESP_KEY			0x0100
#define RPMB_RESP_WCOUNTER		0x0200
#define RPMB_RESP_WRITE_DATA	0x0300
#define RPMB_RESP_READ_DATA		0x0400

/* Error codes */
#define RPMB_OK					0
#define RPMB_ERR_GENERAL		1
#define RPMB_ERR_AUTH			2
#define RPMB_ERR_COUNTER		3
#define RPMB_ERR_ADDRESS		4
#define RPMB_ERR_WRITE			5
#define RPMB_ERR_READ			6
#define RPMB_ERR_KEY			7
#define RPMB_ERR_CNT_EXPIRED	0x80
#define RPMB_ERR_MSK			0x7

/* Sizes of RPMB data frame */
#define RPMB_SZ_STUFF		196
#define RPMB_SZ_MAC			32
#define RPMB_SZ_DATA		256
#define RPMB_SZ_NONCE		16

#define SHA256_BLOCK_SIZE	64

/* Structure of RPMB data frame. */
struct s_rpmb {
	unsigned char stuff[RPMB_SZ_STUFF];
	unsigned char mac[RPMB_SZ_MAC];
	unsigned char data[RPMB_SZ_DATA];
	unsigned char nonce[RPMB_SZ_NONCE];
	unsigned int write_counter;
	unsigned short address;
	unsigned short block_count;
	unsigned short result;
	unsigned short request;
};

uint16_t rpmb_get_request(struct s_rpmb *rpmb_req);
void rpmb_record_req(struct s_rpmb *rpmb_req);
void rpmb_record_write_req(struct s_rpmb *rpmb_req);

uint32_t rpmb_read_write_counter(void);
uint16_t rpmb_read_address(void);
uint16_t rpmb_read_block_count(void);
uint16_t rpmb_read_request(void);

void rpmb_acquire_status(struct s_rpmb *rsp);
void rpmb_acquire_wcounter(struct s_rpmb *rsp, BlockBackend *blk, uint32_t key_addr);
void rpmb_acquire_data(struct s_rpmb *rsp, BlockBackend *blk, uint32_t boot_capacity);

bool rpmb_key_check(BlockBackend *blk, uint64_t key_addr);
bool rpmb_written_data_check(BlockBackend *blk, uint64_t key_addr, uint32_t rpmb_capacity);
bool rpmb_update_wcounter_into_blk(BlockBackend *blk, uint64_t key_addr);

#endif