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

static struct s_rpmb rpmb_req_frame;
static struct s_rpmb rpmb_write_req;
#define OTP_WRITE_COUNTER_OFFSET    0x30

static inline uint32_t rpmb_get_write_counter(struct s_rpmb *frame)
{
    return be32_to_cpu(frame->write_counter);
}

static inline uint16_t rpmb_get_address(struct s_rpmb *frame)
{
    return be16_to_cpu(frame->address);
}

static inline uint16_t rpmb_get_block_count(struct s_rpmb *frame)
{
    return be16_to_cpu(frame->block_count);
}

static inline uint16_t rpmb_get_result(struct s_rpmb *frame)
{
    return be16_to_cpu(frame->result);
}

uint16_t rpmb_get_request(struct s_rpmb *rpmb_req)
{
    uint16_t request = be16_to_cpu(rpmb_req->request);

#ifdef RPMB_DEBUG
    qemu_hexdump(stderr, "rpmb frame", rpmb_req, sizeof(*rpmb_req));
#endif

    switch (request) {
    case RPMB_REQ_KEY ... RPMB_REQ_STATUS:
        return request;
    default:
        qemu_log_mask(LOG_STRACE, "Invalid RPMB request type\n");
    }

    return 0;
}

void rpmb_record_req(struct s_rpmb *rpmb_req)
{
    memset(&rpmb_req_frame, 0, sizeof(struct s_rpmb));

    memcpy(&rpmb_req_frame, rpmb_req, sizeof(struct s_rpmb));
}

void rpmb_record_write_req(struct s_rpmb *rpmb_req)
{
    memset(&rpmb_write_req, 0, sizeof(struct s_rpmb));

    memcpy(&rpmb_write_req, rpmb_req, sizeof(struct s_rpmb));
}

uint32_t rpmb_read_write_counter(void)
{
    return rpmb_get_write_counter(&rpmb_req_frame);
}

uint16_t rpmb_read_address(void)
{
    return rpmb_get_address(&rpmb_req_frame);
}

uint16_t rpmb_read_block_count(void)
{
    return rpmb_get_block_count(&rpmb_req_frame);
}

uint16_t rpmb_read_request(void)
{
    return rpmb_get_request(&rpmb_req_frame);
}

static inline void rpmb_set_result(uint16_t err_code)
{
    rpmb_write_req.result = cpu_to_be16(err_code);
}

static inline bool rpmb_read_key_from_blk(BlockBackend *blk, uint64_t key_addr, uint8_t *key_buff)
{
    unsigned char key[RPMB_SZ_MAC + 1];

    if (!blk)
        return false;

    if (blk_pread(blk, key_addr, RPMB_SZ_MAC + 1, key, 0) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to read key\n");
        return false;
    }

    if (key[RPMB_SZ_MAC]) {
        if (key_buff)
            memcpy(key_buff, key, RPMB_SZ_MAC);

        return true;
    }

    return false;
}

static bool rpmb_read_wcounter_from_blk(BlockBackend *blk, uint64_t key_addr, uint32_t *wcounter)
{
    uint32_t addr = key_addr + OTP_WRITE_COUNTER_OFFSET;
    uint32_t counter;

    if (!blk)
        return false;

    if (blk_pread(blk, addr, sizeof(counter), &counter, 0) < 0) {
        return false;
    }

    if (wcounter)
        *wcounter = be32_to_cpu(counter);

    return true;
}

void rpmb_acquire_status(struct s_rpmb *respones)
{
    uint16_t request = rpmb_get_request(&rpmb_write_req);

    switch (request) {
    case RPMB_REQ_KEY:
        respones->request = cpu_to_be16(RPMB_RESP_KEY);
        break;
    case RPMB_REQ_WRITE_DATA:
        respones->request = cpu_to_be16(RPMB_RESP_WRITE_DATA);
        respones->write_counter =
            cpu_to_be32(rpmb_get_write_counter(&rpmb_write_req));
        break;
    }

    respones->result = cpu_to_be16(rpmb_get_result(&rpmb_write_req));

    /* Clear recorded rpmb request frame */
    memset(&rpmb_req_frame, 0, sizeof(rpmb_req_frame));
}

void rpmb_acquire_wcounter(struct s_rpmb *respones, BlockBackend *blk, uint32_t key_addr)
{
    uint32_t counter;

    respones->request = cpu_to_be16(RPMB_RESP_WCOUNTER);

    if (!blk) {
        respones->result = cpu_to_be16(RPMB_ERR_GENERAL);
        return;
    }

    if (!rpmb_read_wcounter_from_blk(blk, key_addr, &counter)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to read write counter.\n");
        respones->result = cpu_to_be16(RPMB_ERR_READ);
        return;
    }

    respones->write_counter = cpu_to_be32(counter);
}

bool rpmb_key_check(BlockBackend *blk, uint64_t key_addr)
{
    if (!blk) {
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    }

    if (rpmb_read_key_from_blk(blk, key_addr, NULL)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Key already programmed\n");
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    };

    return true;
}

static uint8_t *rpmb_hmac_digest(const char *buff, uint64_t len, const uint8_t *key, uint16_t key_size)
{
    QCryptoHmac *hmac = NULL;
    uint8_t *result = NULL;
    unsigned char *mac = g_new0(unsigned char, RPMB_SZ_MAC);
    char str[9];
    uint32_t tmp = 0;
    int ret, i;

    hmac = qcrypto_hmac_new(QCRYPTO_HASH_ALG_SHA256, key,
                                key_size, &error_fatal);
    if (!hmac)
        return NULL;

    ret = qcrypto_hmac_digest(hmac, buff, len,
                            (char **)&result, &error_fatal);
    if (ret) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to initialize qcrypto.\n");
        return NULL;
    }

    for (i = 0; i < RPMB_SZ_MAC / sizeof(tmp); i++) {
        memcpy(str, result+(i * 8), 8);
        qemu_strtoui(str, NULL, 16, &tmp);
        tmp = cpu_to_be32(tmp);
        memcpy(mac + (i * sizeof(tmp)), &tmp, sizeof(tmp));
    }
    qcrypto_hmac_free(hmac);
    g_free(result);

    return mac;
}

bool rpmb_written_data_check(BlockBackend *blk, uint64_t key_addr, uint32_t rpmb_capacity)
{
    uint32_t req_wcounter, counter;
    unsigned char key[RPMB_SZ_MAC];
    uint8_t *mac;
    uint64_t addr;

    addr = (rpmb_read_address() + 1) * RPMB_SZ_DATA;
    if (addr >= rpmb_capacity) {
        rpmb_set_result(RPMB_ERR_ADDRESS);
        return false;
    }

    if (!blk) {
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    }

    req_wcounter = rpmb_read_write_counter();
    if (req_wcounter >= UINT32_MAX) {
        rpmb_set_result(RPMB_ERR_COUNTER);
        return false;
    }

    if (!rpmb_read_wcounter_from_blk(blk, key_addr, &counter)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to get counter\n");
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    }

    if (req_wcounter != counter) {
        rpmb_set_result(RPMB_ERR_COUNTER);
        return false;
    }

    if (!rpmb_read_key_from_blk(blk, key_addr, key)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to get key.\n");
        rpmb_set_result(RPMB_ERR_KEY);
        return false;
    }

    mac = rpmb_hmac_digest((const char*)rpmb_req_frame.data, 284, key, RPMB_SZ_MAC);
    if (!mac) {
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    }

    if (memcmp(rpmb_req_frame.mac, mac, RPMB_SZ_MAC)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Both MAC mismatch\n");
        rpmb_set_result(RPMB_ERR_AUTH);
        return false;
    }
    g_free(mac);

    return true;
}

bool rpmb_update_wcounter_into_blk(BlockBackend *blk, uint64_t key_addr)
{
    uint32_t wcounter;

    if (!blk) {
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    }

    if (!rpmb_read_wcounter_from_blk(blk, key_addr, &wcounter)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to get counter\n");
        rpmb_set_result(RPMB_ERR_GENERAL);
        return false;
    }

    wcounter++;
    wcounter = cpu_to_be32(wcounter);
    key_addr += OTP_WRITE_COUNTER_OFFSET;
    if (blk_pwrite(blk, key_addr, sizeof(wcounter), &wcounter, 0) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to write counter\n");
        rpmb_set_result(RPMB_ERR_WRITE);
        return false;
    }

    rpmb_write_req.write_counter = wcounter;

    return true;
}

void rpmb_acquire_data(struct s_rpmb *respone, BlockBackend *blk, uint32_t boot_capacity)
{
    unsigned char key[RPMB_SZ_MAC];
    uint8_t *mac;
    uint64_t addr, key_addr;

    respone->request = cpu_to_be16(RPMB_RESP_READ_DATA);

    if (!blk) {
        respone->result = cpu_to_be16(RPMB_ERR_GENERAL);
        return;
    }

    addr = key_addr = 2 * boot_capacity;
    addr += (rpmb_read_address() + 1) * RPMB_SZ_DATA;
    if (blk_pread(blk, addr, RPMB_SZ_DATA, respone->data, 0) < 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to read data.\n");
        respone->result = cpu_to_be16(RPMB_ERR_READ);
        return;
    }

    if (!rpmb_read_key_from_blk(blk, key_addr, key)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Failed to get key.\n");
        respone->result = cpu_to_be16(RPMB_ERR_KEY);
        return;
    }

    mac = rpmb_hmac_digest((const char*)respone->data, 284, key, RPMB_SZ_MAC);
    if (!mac) {
        respone->result = cpu_to_be16(RPMB_ERR_GENERAL);
        return;
    }
    memcpy(respone->mac, mac, RPMB_SZ_MAC);
    g_free(mac);

    rpmb_req_frame.address = cpu_to_be16(((addr - boot_capacity))/RPMB_SZ_DATA);
}
