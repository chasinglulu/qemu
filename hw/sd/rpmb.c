// SPDX-License-Identifier: GPL-2.0+
/*
 * eMMC- Replay Protected Memory Block
 * According to JEDEC Standard No. 84-A441
 */

#include "hw/sd/rpmb.h"
#include "qemu/log.h"
#include "qemu/cutils.h"
#include "qemu/bswap.h"

#define RPMB_DEBUG   1

/* Error messages */
#if 0
static const char * const rpmb_err_msg[] = {
	"",
	"General failure",
	"Authentication failure",
	"Counter failure",
	"Address failure",
	"Write failure",
	"Read failure",
	"Authentication key not yet programmed",
};
#endif

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

void rpmb_blk_read(struct s_rpmb *rpmb_frame)
{
#ifdef RPMB_DEBUG
    qemu_hexdump(stderr, "rpmb frame read", rpmb_frame, sizeof(*rpmb_frame));
#endif
}

uint rpmb_blk_write(struct s_rpmb *rpmb_frame)
{
#ifdef RPMB_DEBUG
    qemu_hexdump(stderr, "rpmb frame write", rpmb_frame, sizeof(*rpmb_frame));
#endif
    return 0;
}