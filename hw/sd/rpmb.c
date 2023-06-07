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
#include "crypto/hmac.h"
#include "qapi/error.h"

//#define RPMB_DEBUG   1

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

uint32_t rpmb_get_write_counter(struct s_rpmb *rpmb_frame)
{
    return be32_to_cpu(rpmb_frame->write_counter);
}

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

bool rpmb_check_write(struct s_rpmb *rpmb_frame, BlockBackend *blk, uint64_t key_addr, uint32_t rpmb_capacity)
{
    uint32_t write_counter = rpmb_get_write_counter(rpmb_frame);
    uint32_t counter;
    unsigned char key[RPMB_SZ_MAC + 1];
    QCryptoHmac *hmac = NULL;
    uint8_t *result = NULL;
    unsigned char mac[RPMB_SZ_MAC];
    char str[9];
    uint32_t tmp = 0;
    int ret, i;
    uint64_t addr;

    addr = (rpmb_get_address(rpmb_frame) + 1) * RPMB_SZ_DATA;
    if (addr >= rpmb_capacity) {
        rpmb_set_result(RPMB_ERR_ADDRESS);
        return false;
    }

    if (!blk) {
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    }

    if (write_counter >= UINT32_MAX) {
        rpmb_set_result(RPMB_ERR_COUNTER);
        return false;
    }

    if (blk_pread(blk, key_addr+OTP_WRITE_COUNTER_OFFSET, sizeof(counter), &counter, 0) < 0) {
        qemu_log("Read counter failed\n");
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    }

    if (write_counter != counter) {
        rpmb_set_result(RPMB_ERR_COUNTER);
        return false;
    }

    if (blk_pread(blk, key_addr, RPMB_SZ_MAC + 1, key, 0) < 0) {
        qemu_log("Read key failed\n");
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    }

    if (!key[RPMB_SZ_MAC]) {
        rpmb_set_result(RPMB_ERR_AUTH);
        return false;
    }

    hmac = qcrypto_hmac_new(QCRYPTO_HASH_ALG_SHA256, (const uint8_t *)key,
                                RPMB_SZ_MAC, &error_fatal);
    g_assert(hmac != NULL);

    ret = qcrypto_hmac_digest(hmac, (const char *)rpmb_frame->data,
                                  284, (char **)&result,
                                  &error_fatal);
    if (ret) {
        qemu_log("crypto digest failed\n");
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    }

    for (i = 0; i < RPMB_SZ_MAC / sizeof(tmp); i++) {
        memcpy(str, result+(i * 8), 8);
        qemu_strtoui(str, NULL, 16, &tmp);
        tmp = cpu_to_be32(tmp);
        memcpy(mac + (i * sizeof(tmp)), &tmp, sizeof(tmp));
    }

    if (memcmp(rpmb_frame->mac, mac, RPMB_SZ_MAC)) {
        qemu_log("mac failed\n");
        rpmb_set_result(RPMB_ERR_AUTH);
        return false;
    }

    return true;
}

bool rpmb_update_write_counter(BlockBackend *blk, uint64_t key_addr, uint32_t counter)
{
    if (!blk) {
        rpmb_set_result(RPMB_ERR_WRITE);
        return false;
    }

    counter++;
    if (blk_pwrite(blk, key_addr + OTP_WRITE_COUNTER_OFFSET, sizeof(counter), &counter, 0) < 0) {
        qemu_log("Write counter failed\n");
        rpmb_set_result(RPMB_ERR_WRITE);
        return false;
    }

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

void rpmb_hmac(struct s_rpmb *resp, BlockBackend *blk, uint64_t key_addr)
{
    QCryptoHmac *hmac = NULL;
    unsigned char key[RPMB_SZ_MAC + 1];
    uint8_t *result = NULL;
    char str[9];
    uint32_t tmp = 0;
    int ret, i;

    if (blk_pread(blk, key_addr, RPMB_SZ_MAC + 1, key, 0) < 0) {
        qemu_log("Read key failed\n");
        return;
    }

    if (!key[RPMB_SZ_MAC])
        return;

    hmac = qcrypto_hmac_new(QCRYPTO_HASH_ALG_SHA256, (const uint8_t *)key,
                                RPMB_SZ_MAC, &error_fatal);
    g_assert(hmac != NULL);

    ret = qcrypto_hmac_digest(hmac, (const char *)resp->data,
                                  284, (char **)&result,
                                  &error_fatal);
    if (ret) {
        qemu_log("crypto digest failed\n");
        return;
    }

    for (i = 0; i < RPMB_SZ_MAC / sizeof(tmp); i++) {
        memcpy(str, result+(i * 8), 8);
        qemu_strtoui(str, NULL, 16, &tmp);
        tmp = cpu_to_be32(tmp);
        memcpy(resp->mac + (i * sizeof(tmp)), &tmp, sizeof(tmp));
    }
}

void rpmb_read_data(struct s_rpmb *respones, BlockBackend *blk, uint64_t addr, uint32_t boot_cap)
{
    respones->request = cpu_to_be16(RPMB_RESP_READ_DATA);

    if (!blk) {
        respones->result = cpu_to_be16(RPMB_ERR_GENERAL);
        return;
    }

    if (blk_pread(blk, addr, RPMB_SZ_DATA, respones->data, 0) < 0) {
        qemu_log("Read counter failed\n");
        respones->result = cpu_to_be16(RPMB_ERR_READ);
        return;
    }

    rpmb_hmac(respones, blk, boot_cap);
}
