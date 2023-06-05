// SPDX-License-Identifier: GPL-2.0+
/*
 * eMMC- Replay Protected Memory Block
 * According to JEDEC Standard No. 84-A441
 */

#include "hw/sd/rpmb.h"
#include "qemu/log.h"
#include "qemu/cutils.h"
#include "qemu/bswap.h"
#include "sysemu/block-backend-io.h"

#define RPMB_DEBUG   1

static struct s_rpmb rpmb_write_frame;
#define OTP_WRITE_COUNTER_OFFSET    0x30

uint16_t rpmb_get_request(struct s_rpmb *rpmb_frame)
{
    uint16_t request = be16_to_cpu(rpmb_frame->request);

#ifdef RPMB_DEBUG
    qemu_hexdump(stderr, "rpmb frame", rpmb_frame, sizeof(*rpmb_frame));
#endif

    switch (request) {
    case RPMB_REQ_KEY ... RPMB_REQ_STATUS:
        return request;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid RPMB request type\n");
    }

    return 0;
}

uint16_t rpmb_get_address(struct s_rpmb *rpmb_frame)
{
    return be16_to_cpu(rpmb_frame->address);
}

uint16_t rpmb_get_block_count(struct s_rpmb *rpmb_frame)
{
    return be16_to_cpu(rpmb_frame->block_count);
}

#if 0
inline uint16_t rpmb_get_write_counter(struct s_rpmb *rpmb_frame)
{
    return be16_to_cpu(rpmb_frame->write_counter);
}

inline uint16_t rpmb_get_result(struct s_rpmb *rpmb_frame)
{
    return be16_to_cpu(rpmb_frame->result);
}
#endif

void rpmb_write(struct s_rpmb *rpmb_frame)
{
    memset(&rpmb_write_frame, 0, sizeof(struct s_rpmb));

    memcpy(&rpmb_write_frame, rpmb_frame, sizeof(struct s_rpmb));
}

void rpmb_read_status(struct s_rpmb *respones)
{
    uint16_t request = be16_to_cpu(rpmb_write_frame.request);

    switch (request) {
    case RPMB_REQ_KEY:
        respones->request = cpu_to_be16(RPMB_RESP_KEY);
        break;
    case RPMB_REQ_WRITE_DATA:
        respones->request = cpu_to_be16(RPMB_RESP_WRITE_DATA);
        break;
    }

    respones->result = rpmb_write_frame.result;
    memset(&rpmb_write_frame, 0, sizeof(rpmb_write_frame));
}

bool rpmb_check_key(BlockBackend *blk, uint64_t addr)
{
    unsigned char key[RPMB_SZ_MAC + 1];

    if (!blk)
        return false;

    if (blk_pread(blk, addr, RPMB_SZ_MAC + 1, key, 0) < 0) {
        qemu_log("Read key failed\n");
        return false;
    }

    if (key[RPMB_SZ_MAC])
        return false;

    return true;
}

void rpmb_set_result(uint16_t err_code)
{
    rpmb_write_frame.result = cpu_to_be16(err_code);
}

void rpmb_read_write_counter(struct s_rpmb *respones, BlockBackend *blk)
{
    uint32_t counter;

    respones->request = cpu_to_be16(RPMB_RESP_WCOUNTER);

    if (!blk) {
        respones->result = cpu_to_be16(RPMB_ERR_GENERAL);
        return;
    }

    if (blk_pread(blk, OTP_WRITE_COUNTER_OFFSET, sizeof(counter), &counter, 0) < 0) {
        qemu_log("Read counter failed\n");
        respones->result = cpu_to_be16(RPMB_ERR_READ);
        return;
    }

    respones->write_counter = cpu_to_be32(counter);
}