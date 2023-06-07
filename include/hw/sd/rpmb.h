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

uint16_t rpmb_get_request(struct s_rpmb *rpmb_frame);
uint16_t rpmb_get_address(struct s_rpmb *rpmb_frame);
uint16_t rpmb_get_block_count(struct s_rpmb *rpmb_frame);
uint32_t rpmb_get_write_counter(struct s_rpmb *rpmb_frame);

void rpmb_write(struct s_rpmb *rpmb_frame);
void rpmb_read_status(struct s_rpmb *respones);
bool rpmb_check_key(BlockBackend *blk, uint64_t addr);
void rpmb_set_result(uint16_t err_code);
void rpmb_read_write_counter(struct s_rpmb *respones, BlockBackend *blk);
void rpmb_read_data(struct s_rpmb *respones, BlockBackend *blk, uint64_t addr, uint32_t boot_cap);
void rpmb_hmac(struct s_rpmb *resp, BlockBackend *blk, uint64_t addr);
bool rpmb_check_write(struct s_rpmb *rpmb_frame, BlockBackend *blk, uint64_t key_addr, uint32_t rpmb_capacity);
bool rpmb_update_write_counter(BlockBackend *blk, uint64_t key_addr, uint32_t count);

#endif
