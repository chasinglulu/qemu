/*
 * QEMU Universal Flash Storage (UFS) Controller
 *
 * Copyright (c) 2023 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Written by Jeuk Kim <jeuk20.kim@samsung.com>
 *
 * This code is licensed under the GNU GPL v2 or later.
 */

/**
 * Reference Specs: https://www.jedec.org/, 3.1
 *
 * Usage
 * -----
 *
 * Add options:
 *      -drive file=<file>,if=none,id=<drive_id>
 *      -device ufs,serial=<serial>,id=<bus_name>, \
 *              nutrs=<N[optional]>,nutmrs=<N[optional]>
 *      -device ufs-lu,drive=<drive_id>,bus=<bus_name>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "ufs.h"

/* The QEMU-UFS device follows spec version 3.1 */
#define UFS_SPEC_VER 0x00000310
#define UFS_MAX_NUTRS 32
#define UFS_MAX_NUTMRS 8

static MemTxResult ufs_addr_read(UfsHc *u, hwaddr addr, void *buf, int size)
{
    uint32_t cap = ldl_le_p(&u->reg.cap);
    hwaddr hi = addr + size - 1;

    if (hi < addr) {
        return MEMTX_DECODE_ERROR;
    }

    if (!UFS_CAP_64AS(cap) && (hi >> 32)) {
        return MEMTX_DECODE_ERROR;
    }

    return pci_dma_read(PCI_DEVICE(u), addr, buf, size);
}

static MemTxResult ufs_addr_write(UfsHc *u, hwaddr addr, const void *buf,
                                  int size)
{
    uint32_t cap = ldl_le_p(&u->reg.cap);
    hwaddr hi = addr + size - 1;
    if (hi < addr) {
        return MEMTX_DECODE_ERROR;
    }

    if (!UFS_CAP_64AS(cap) && (hi >> 32)) {
        return MEMTX_DECODE_ERROR;
    }

    return pci_dma_write(PCI_DEVICE(u), addr, buf, size);
}

static void ufs_complete_req(UfsRequest *req, UfsReqResult req_result);

static inline hwaddr ufs_get_utrd_addr(UfsHc *u, uint32_t slot)
{
    uint32_t utrlba = ldl_le_p(&u->reg.utrlba);
    uint32_t utrlbau = ldl_le_p(&u->reg.utrlbau);
    hwaddr utrl_base_addr = (((hwaddr)utrlbau) << 32) + utrlba;
    hwaddr utrd_addr = utrl_base_addr + slot * sizeof(UtpTransferReqDesc);

    return utrd_addr;
}

static inline hwaddr ufs_get_req_upiu_base_addr(const UtpTransferReqDesc *utrd)
{
    uint32_t cmd_desc_base_addr_lo =
        le32_to_cpu(utrd->command_desc_base_addr_lo);
    uint32_t cmd_desc_base_addr_hi =
        le32_to_cpu(utrd->command_desc_base_addr_hi);

    return (((hwaddr)cmd_desc_base_addr_hi) << 32) + cmd_desc_base_addr_lo;
}

static inline hwaddr ufs_get_rsp_upiu_base_addr(const UtpTransferReqDesc *utrd)
{
    hwaddr req_upiu_base_addr = ufs_get_req_upiu_base_addr(utrd);
    uint32_t rsp_upiu_byte_off =
        le16_to_cpu(utrd->response_upiu_offset) * sizeof(uint32_t);
    return req_upiu_base_addr + rsp_upiu_byte_off;
}

static MemTxResult ufs_dma_read_utrd(UfsRequest *req)
{
    UfsHc *u = req->hc;
    hwaddr utrd_addr = ufs_get_utrd_addr(u, req->slot);
    MemTxResult ret;

    ret = ufs_addr_read(u, utrd_addr, &req->utrd, sizeof(req->utrd));
    if (ret) {
        trace_ufs_err_dma_read_utrd(req->slot, utrd_addr);
    }
    return ret;
}

static MemTxResult ufs_dma_read_req_upiu(UfsRequest *req)
{
    UfsHc *u = req->hc;
    hwaddr req_upiu_base_addr = ufs_get_req_upiu_base_addr(&req->utrd);
    UtpUpiuReq *req_upiu = &req->req_upiu;
    uint32_t copy_size;
    uint16_t data_segment_length;
    MemTxResult ret;

    /*
     * To know the size of the req_upiu, we need to read the
     * data_segment_length in the header first.
     */
    ret = ufs_addr_read(u, req_upiu_base_addr, &req_upiu->header,
                        sizeof(UtpUpiuHeader));
    if (ret) {
        trace_ufs_err_dma_read_req_upiu(req->slot, req_upiu_base_addr);
        return ret;
    }
    data_segment_length = be16_to_cpu(req_upiu->header.data_segment_length);

    copy_size = sizeof(UtpUpiuHeader) + UFS_TRANSACTION_SPECIFIC_FIELD_SIZE +
                data_segment_length;

    ret = ufs_addr_read(u, req_upiu_base_addr, &req->req_upiu, copy_size);
    if (ret) {
        trace_ufs_err_dma_read_req_upiu(req->slot, req_upiu_base_addr);
    }
    return ret;
}

static MemTxResult ufs_dma_read_prdt(UfsRequest *req)
{
    UfsHc *u = req->hc;
    uint16_t prdt_len = le16_to_cpu(req->utrd.prd_table_length);
    uint16_t prdt_byte_off =
        le16_to_cpu(req->utrd.prd_table_offset) * sizeof(uint32_t);
    uint32_t prdt_size = prdt_len * sizeof(UfshcdSgEntry);
    UfshcdSgEntry *prd_entries;
    hwaddr req_upiu_base_addr, prdt_base_addr;
    int err;

    assert(!req->sg);

    if (prdt_len == 0) {
        return MEMTX_OK;
    }

    prd_entries = g_new(UfshcdSgEntry, prdt_size);
    if (!prd_entries) {
        trace_ufs_err_memory_allocation();
        return MEMTX_ERROR;
    }

    req_upiu_base_addr = ufs_get_req_upiu_base_addr(&req->utrd);
    prdt_base_addr = req_upiu_base_addr + prdt_byte_off;

    err = ufs_addr_read(u, prdt_base_addr, prd_entries, prdt_size);
    if (err) {
        trace_ufs_err_dma_read_prdt(req->slot, prdt_base_addr);
        return err;
    }

    req->sg = g_malloc0(sizeof(QEMUSGList));
    if (!req->sg) {
        trace_ufs_err_memory_allocation();
        g_free(prd_entries);
        return MEMTX_ERROR;
    }
    pci_dma_sglist_init(req->sg, PCI_DEVICE(u), prdt_len);

    for (uint16_t i = 0; i < prdt_len; ++i) {
        hwaddr data_dma_addr = le64_to_cpu(prd_entries[i].addr);
        int32_t data_byte_count = le32_to_cpu(prd_entries[i].size) + 1;
        qemu_sglist_add(req->sg, data_dma_addr, data_byte_count);
    }
    g_free(prd_entries);

    return MEMTX_OK;
}

static MemTxResult ufs_dma_read_upiu(UfsRequest *req)
{
    MemTxResult ret;

    ret = ufs_dma_read_utrd(req);
    if (ret) {
        return ret;
    }

    ret = ufs_dma_read_req_upiu(req);
    if (ret) {
        return ret;
    }

    ret = ufs_dma_read_prdt(req);
    if (ret) {
        return ret;
    }

    return 0;
}

static MemTxResult ufs_dma_write_utrd(UfsRequest *req)
{
    UfsHc *u = req->hc;
    hwaddr utrd_addr = ufs_get_utrd_addr(u, req->slot);
    MemTxResult ret;

    ret = ufs_addr_write(u, utrd_addr, &req->utrd, sizeof(req->utrd));
    if (ret) {
        trace_ufs_err_dma_write_utrd(req->slot, utrd_addr);
    }
    return ret;
}

static MemTxResult ufs_dma_write_rsp_upiu(UfsRequest *req)
{
    UfsHc *u = req->hc;
    hwaddr rsp_upiu_base_addr = ufs_get_rsp_upiu_base_addr(&req->utrd);
    uint32_t rsp_upiu_byte_len =
        le16_to_cpu(req->utrd.response_upiu_length) * sizeof(uint32_t);
    uint16_t data_segment_length =
        be16_to_cpu(req->rsp_upiu.header.data_segment_length);
    uint32_t copy_size = sizeof(UtpUpiuHeader) +
                         UFS_TRANSACTION_SPECIFIC_FIELD_SIZE +
                         data_segment_length;
    MemTxResult ret;

    if (copy_size > rsp_upiu_byte_len) {
        copy_size = rsp_upiu_byte_len;
    }

    ret = ufs_addr_write(u, rsp_upiu_base_addr, &req->rsp_upiu, copy_size);
    if (ret) {
        trace_ufs_err_dma_write_rsp_upiu(req->slot, rsp_upiu_base_addr);
    }
    return ret;
}

static MemTxResult ufs_dma_write_upiu(UfsRequest *req)
{
    MemTxResult ret;

    ret = ufs_dma_write_rsp_upiu(req);
    if (ret) {
        return ret;
    }

    return ufs_dma_write_utrd(req);
}

static void ufs_irq_check(UfsHc *u)
{
    PCIDevice *pci = PCI_DEVICE(u);
    uint32_t is = ldl_le_p(&u->reg.is);
    uint32_t ie = ldl_le_p(&u->reg.ie);

    if ((is & UFS_INTR_MASK) & ie) {
        trace_ufs_irq_raise();
        pci_irq_assert(pci);
    } else {
        trace_ufs_irq_lower();
        pci_irq_deassert(pci);
    }
}

static void ufs_process_db(UfsHc *u, uint64_t val)
{
    uint32_t slot;
    uint32_t nutrs = u->params.nutrs;
    uint32_t utrldbr = ldl_le_p(&u->reg.utrldbr);
    UfsRequest *req;

    val &= ~utrldbr;
    if (!val) {
        return;
    }
    stl_le_p(&u->reg.utrldbr, utrldbr | val);

    slot = find_first_bit(&val, nutrs);

    while (slot < nutrs) {
        req = &u->req_list[slot];
        if (req->state != UFS_REQUEST_IDLE) {
            trace_ufs_err_utrl_slot_busy(req->slot);
            return;
        }

        trace_ufs_process_db(slot);
        req->state = UFS_REQUEST_READY;
        slot = find_next_bit(&val, nutrs, slot + 1);
    }

    qemu_bh_schedule(u->doorbell_bh);
}

static void ufs_process_uiccmd(UfsHc *u, uint32_t val)
{
    uint32_t is = ldl_le_p(&u->reg.is);
    uint32_t hcs = ldl_le_p(&u->reg.hcs);
    uint32_t ucmdarg1 = ldl_le_p(&u->reg.ucmdarg1);
    uint32_t ucmdarg2 = ldl_le_p(&u->reg.ucmdarg2);
    uint32_t ucmdarg3 = ldl_le_p(&u->reg.ucmdarg3);

    trace_ufs_process_uiccmd(val, ucmdarg1, ucmdarg2, ucmdarg3);
    /*
     * Only the essential uic commands for running drivers on Linux and Windows
     * are implemented.
     */
    switch (val) {
    case UIC_CMD_DME_LINK_STARTUP:
        UFS_HCS_SET_DP(hcs, 1);
        UFS_HCS_SET_UTRLRDY(hcs, 1);
        UFS_HCS_SET_UTMRLRDY(hcs, 1);
        ucmdarg2 = UIC_CMD_RESULT_SUCCESS;
        break;
    /* TODO: Revisit it when Power Management is implemented */
    case UIC_CMD_DME_HIBER_ENTER:
        UFS_IS_SET_UHES(is, 1);
        UFS_HCS_SET_UPMCRS(hcs, PWR_LOCAL);
        ucmdarg2 = UIC_CMD_RESULT_SUCCESS;
        break;
    case UIC_CMD_DME_HIBER_EXIT:
        UFS_IS_SET_UHXS(is, 1);
        UFS_HCS_SET_UPMCRS(hcs, PWR_LOCAL);
        ucmdarg2 = UIC_CMD_RESULT_SUCCESS;
        break;
    default:
        ucmdarg2 = UIC_CMD_RESULT_FAILURE;
    }

    UFS_IS_SET_UCCS(is, 1);

    stl_le_p(&u->reg.is, is);
    stl_le_p(&u->reg.hcs, hcs);
    stl_le_p(&u->reg.ucmdarg1, ucmdarg1);
    stl_le_p(&u->reg.ucmdarg2, ucmdarg2);
    stl_le_p(&u->reg.ucmdarg3, ucmdarg3);

    ufs_irq_check(u);
}

static void ufs_write_reg(UfsHc *u, hwaddr offset, uint32_t data, unsigned size)
{
    uint32_t is = ldl_le_p(&u->reg.is);
    uint32_t hcs = ldl_le_p(&u->reg.hcs);
    uint32_t hce = ldl_le_p(&u->reg.hce);
    uint32_t utrldbr = ldl_le_p(&u->reg.utrldbr);
    uint32_t utrlcnr = ldl_le_p(&u->reg.utrlcnr);
    uint32_t utrlba, utmrlba;

    switch (offset) {
    case UFS_REG_IS:
        is &= ~data;
        stl_le_p(&u->reg.is, is);
        ufs_irq_check(u);
        break;
    case UFS_REG_IE:
        stl_le_p(&u->reg.ie, data);
        ufs_irq_check(u);
        break;
    case UFS_REG_HCE:
        if (!UFS_HCE_HCE(hce) && UFS_HCE_HCE(data)) {
            UFS_HCS_SET_UCRDY(hcs, 1);
            UFS_HCE_SET_HCE(hce, 1);
            stl_le_p(&u->reg.hcs, hcs);
            stl_le_p(&u->reg.hce, hce);
        } else if (UFS_HCE_HCE(hce) && !UFS_HCE_HCE(data)) {
            hcs = 0;
            UFS_HCE_SET_HCE(hce, 0);
            stl_le_p(&u->reg.hcs, hcs);
            stl_le_p(&u->reg.hce, hce);
        }
        break;
    case UFS_REG_UTRLBA:
        utrlba = data & (UTRLBA_UTRLBA_MASK << UTRLBA_UTRLBA_SHIFT);
        stl_le_p(&u->reg.utrlba, utrlba);
        break;
    case UFS_REG_UTRLBAU:
        stl_le_p(&u->reg.utrlbau, data);
        break;
    case UFS_REG_UTRLDBR:
        ufs_process_db(u, data);
        utrldbr |= data;
        stl_le_p(&u->reg.utrldbr, utrldbr);
        break;
    case UFS_REG_UTRLRSR:
        stl_le_p(&u->reg.utrlrsr, data);
        break;
    case UFS_REG_UTRLCNR:
        utrlcnr &= ~data;
        stl_le_p(&u->reg.utrlcnr, utrlcnr);
        break;
    case UFS_REG_UTMRLBA:
        utmrlba = data & (UTMRLBA_UTMRLBA_MASK << UTMRLBA_UTMRLBA_SHIFT);
        stl_le_p(&u->reg.utmrlba, utmrlba);
        break;
    case UFS_REG_UTMRLBAU:
        stl_le_p(&u->reg.utmrlbau, data);
        break;
    case UFS_REG_UICCMD:
        ufs_process_uiccmd(u, data);
        break;
    case UFS_REG_UCMDARG1:
        stl_le_p(&u->reg.ucmdarg1, data);
        break;
    case UFS_REG_UCMDARG2:
        stl_le_p(&u->reg.ucmdarg2, data);
        break;
    case UFS_REG_UCMDARG3:
        stl_le_p(&u->reg.ucmdarg3, data);
        break;
    case UFS_REG_UTRLCLR:
    case UFS_REG_UTMRLDBR:
    case UFS_REG_UTMRLCLR:
    case UFS_REG_UTMRLRSR:
        trace_ufs_err_unsupport_register_offset(offset);
        break;
    default:
        trace_ufs_err_invalid_register_offset(offset);
        break;
    }
}

static uint64_t ufs_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    UfsHc *u = (UfsHc *)opaque;
    uint8_t *ptr = (uint8_t *)&u->reg;
    uint64_t value;

    if (addr > sizeof(u->reg) - size) {
        trace_ufs_err_invalid_register_offset(addr);
        return 0;
    }

    value = ldn_le_p(ptr + addr, size);
    trace_ufs_mmio_read(addr, value, size);
    return value;
}

static void ufs_mmio_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    UfsHc *u = (UfsHc *)opaque;

    if (addr > sizeof(u->reg) - size) {
        trace_ufs_err_invalid_register_offset(addr);
        return;
    }

    trace_ufs_mmio_write(addr, data, size);
    ufs_write_reg(u, addr, data, size);
}

static const MemoryRegionOps ufs_mmio_ops = {
    .read = ufs_mmio_read,
    .write = ufs_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static QEMUSGList *ufs_get_sg_list(SCSIRequest *scsi_req)
{
    UfsRequest *req = scsi_req->hba_private;
    return req->sg;
}

static void ufs_build_upiu_sense_data(UfsRequest *req, SCSIRequest *scsi_req)
{
    req->rsp_upiu.sr.sense_data_len = cpu_to_be16(scsi_req->sense_len);
    assert(scsi_req->sense_len <= SCSI_SENSE_LEN);
    memcpy(req->rsp_upiu.sr.sense_data, scsi_req->sense, scsi_req->sense_len);
}

static void ufs_build_upiu_header(UfsRequest *req, uint8_t trans_type,
                                  uint8_t flags, uint8_t response,
                                  uint8_t scsi_status,
                                  uint16_t data_segment_length)
{
    memcpy(&req->rsp_upiu.header, &req->req_upiu.header, sizeof(UtpUpiuHeader));
    req->rsp_upiu.header.trans_type = trans_type;
    req->rsp_upiu.header.flags = flags;
    req->rsp_upiu.header.response = response;
    req->rsp_upiu.header.scsi_status = scsi_status;
    req->rsp_upiu.header.data_segment_length = cpu_to_be16(data_segment_length);
}

static void ufs_scsi_command_complete(SCSIRequest *scsi_req, size_t resid)
{
    UfsRequest *req = scsi_req->hba_private;
    int16_t status = scsi_req->status;
    uint32_t expected_len = be32_to_cpu(req->req_upiu.sc.exp_data_transfer_len);
    uint32_t transfered_len = scsi_req->cmd.xfer - resid;
    uint8_t flags = 0, response = COMMAND_RESULT_SUCESS;
    uint16_t data_segment_length;

    if (expected_len > transfered_len) {
        req->rsp_upiu.sr.residual_transfer_count =
            cpu_to_be32(expected_len - transfered_len);
        flags |= UFS_UPIU_FLAG_UNDERFLOW;
    } else if (expected_len < transfered_len) {
        req->rsp_upiu.sr.residual_transfer_count =
            cpu_to_be32(transfered_len - expected_len);
        flags |= UFS_UPIU_FLAG_OVERFLOW;
    }

    if (status != 0) {
        ufs_build_upiu_sense_data(req, scsi_req);
        response = COMMAND_RESULT_FAIL;
    }

    data_segment_length = cpu_to_be16(scsi_req->sense_len +
                                      sizeof(req->rsp_upiu.sr.sense_data_len));
    ufs_build_upiu_header(req, UPIU_TRANSACTION_RESPONSE, flags, response,
                          status, data_segment_length);

    ufs_complete_req(req, UFS_REQUEST_SUCCESS);

    scsi_req->hba_private = NULL;
    scsi_req_unref(scsi_req);
}

static const struct SCSIBusInfo ufs_scsi_info = {
    .tcq = true,
    .max_target = 0,
    .max_lun = UFS_MAX_LUS,
    .max_channel = 0,

    .get_sg_list = ufs_get_sg_list,
    .complete = ufs_scsi_command_complete,
};

static UfsReqResult ufs_exec_scsi_cmd(UfsRequest *req)
{
    UfsHc *u = req->hc;
    uint8_t lun = req->req_upiu.header.lun;
    uint8_t task_tag = req->req_upiu.header.task_tag;
    SCSIDevice *dev = NULL;

    trace_ufs_exec_scsi_cmd(req->slot, lun, req->req_upiu.sc.cdb[0]);

    if (!is_wlun(lun)) {
        if (lun >= u->device_desc.number_lu) {
            trace_ufs_err_scsi_cmd_invalid_lun(lun);
            ufs_build_upiu_header(req, UPIU_TRANSACTION_RESPONSE,
                                    0, 0, UFS_REQUEST_ERROR, 0);
            return UFS_REQUEST_SUCCESS;
        } else if (u->lus[lun] == NULL) {
            trace_ufs_err_scsi_cmd_invalid_lun(lun);
            ufs_build_upiu_header(req, UPIU_TRANSACTION_RESPONSE,
                                    0, 0, UFS_REQUEST_ERROR, 0);
            return UFS_REQUEST_SUCCESS;
        }
    }

    switch (lun) {
    case UFS_UPIU_REPORT_LUNS_WLUN:
        dev = &u->report_wlu->qdev;
        break;
    case UFS_UPIU_UFS_DEVICE_WLUN:
        dev = &u->dev_wlu->qdev;
        break;
    case UFS_UPIU_BOOT_WLUN:
        dev = &u->boot_wlu->qdev;
        break;
    case UFS_UPIU_RPMB_WLUN:
        dev = &u->rpmb_wlu->qdev;
        break;
    default:
        dev = &u->lus[lun]->qdev;
    }

    SCSIRequest *scsi_req = scsi_req_new(
        dev, task_tag, lun, req->req_upiu.sc.cdb, UFS_CDB_SIZE, req);

    uint32_t len = scsi_req_enqueue(scsi_req);
    if (len) {
        scsi_req_continue(scsi_req);
    }

    return UFS_REQUEST_NO_COMPLETE;
}

static UfsReqResult ufs_exec_nop_cmd(UfsRequest *req)
{
    trace_ufs_exec_nop_cmd(req->slot);
    ufs_build_upiu_header(req, UPIU_TRANSACTION_NOP_IN, 0, 0, 0, 0);
    return UFS_REQUEST_SUCCESS;
}

/*
 * This defines the permission of flags based on their IDN. There are some
 * things that are declared read-only, which is inconsistent with the ufs spec,
 * because we want to return an error for features that are not yet supported.
 */
static const int flag_permission[QUERY_FLAG_IDN_COUNT] = {
    [QUERY_FLAG_IDN_FDEVICEINIT] = UFS_QUERY_FLAG_READ | UFS_QUERY_FLAG_SET,
    /* Write protection is not supported */
    [QUERY_FLAG_IDN_PERMANENT_WPE] = UFS_QUERY_FLAG_READ,
    [QUERY_FLAG_IDN_PWR_ON_WPE] = UFS_QUERY_FLAG_READ,
    [QUERY_FLAG_IDN_BKOPS_EN] = UFS_QUERY_FLAG_READ | UFS_QUERY_FLAG_SET |
                                UFS_QUERY_FLAG_CLEAR | UFS_QUERY_FLAG_TOGGLE,
    [QUERY_FLAG_IDN_LIFE_SPAN_MODE_ENABLE] =
        UFS_QUERY_FLAG_READ | UFS_QUERY_FLAG_SET | UFS_QUERY_FLAG_CLEAR |
        UFS_QUERY_FLAG_TOGGLE,
    /* Purge Operation is not supported */
    [QUERY_FLAG_IDN_PURGE_ENABLE] = UFS_QUERY_FLAG_NONE,
    /* Refresh Operation is not supported */
    [QUERY_FLAG_IDN_REFRESH_ENABLE] = UFS_QUERY_FLAG_NONE,
    /* Physical Resource Removal is not supported */
    [QUERY_FLAG_IDN_FPHYRESOURCEREMOVAL] = UFS_QUERY_FLAG_READ,
    [QUERY_FLAG_IDN_BUSY_RTC] = UFS_QUERY_FLAG_READ,
    [QUERY_FLAG_IDN_PERMANENTLY_DISABLE_FW_UPDATE] = UFS_QUERY_FLAG_READ,
    /* Write Booster is not supported */
    [QUERY_FLAG_IDN_WB_EN] = UFS_QUERY_FLAG_READ,
    [QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN] = UFS_QUERY_FLAG_READ,
    [QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8] = UFS_QUERY_FLAG_READ,
};

static inline QueryRespCode ufs_flag_check_idn_valid(uint8_t idn, int op)
{
    if (idn >= QUERY_FLAG_IDN_COUNT) {
        return QUERY_RESULT_INVALID_IDN;
    }

    if (!(flag_permission[idn] & op)) {
        if (op == UFS_QUERY_FLAG_READ) {
            trace_ufs_err_query_flag_not_readable(idn);
            return QUERY_RESULT_NOT_READABLE;
        }
        trace_ufs_err_query_flag_not_writable(idn);
        return QUERY_RESULT_NOT_WRITEABLE;
    }

    return QUERY_RESULT_SUCCESS;
}

static const int attr_permission[QUERY_ATTR_IDN_COUNT] = {
    /* booting is not supported */
    [QUERY_ATTR_IDN_BOOT_LU_EN] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_POWER_MODE] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_ACTIVE_ICC_LVL] =
        UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [QUERY_ATTR_IDN_OOO_DATA_EN] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_BKOPS_STATUS] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_PURGE_STATUS] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_MAX_DATA_IN] = UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [QUERY_ATTR_IDN_MAX_DATA_OUT] = UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [QUERY_ATTR_IDN_DYN_CAP_NEEDED] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_REF_CLK_FREQ] = UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [QUERY_ATTR_IDN_CONF_DESC_LOCK] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_MAX_NUM_OF_RTT] =
        UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [QUERY_ATTR_IDN_EE_CONTROL] = UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [QUERY_ATTR_IDN_EE_STATUS] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_SECONDS_PASSED] = UFS_QUERY_ATTR_WRITE,
    [QUERY_ATTR_IDN_CNTX_CONF] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_FFU_STATUS] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_PSA_STATE] = UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [QUERY_ATTR_IDN_PSA_DATA_SIZE] = UFS_QUERY_ATTR_READ | UFS_QUERY_ATTR_WRITE,
    [QUERY_ATTR_IDN_REF_CLK_GATING_WAIT_TIME] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_CASE_ROUGH_TEMP] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_HIGH_TEMP_BOUND] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_LOW_TEMP_BOUND] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_THROTTLING_STATUS] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_WB_FLUSH_STATUS] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE] = UFS_QUERY_ATTR_READ,
    /* refresh operation is not supported */
    [QUERY_ATTR_IDN_REFRESH_STATUS] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_REFRESH_FREQ] = UFS_QUERY_ATTR_READ,
    [QUERY_ATTR_IDN_REFRESH_UNIT] = UFS_QUERY_ATTR_READ,
};

static inline QueryRespCode ufs_attr_check_idn_valid(uint8_t idn, int op)
{
    if (idn >= QUERY_ATTR_IDN_COUNT) {
        return QUERY_RESULT_INVALID_IDN;
    }

    if (!(attr_permission[idn] & op)) {
        if (op == UFS_QUERY_ATTR_READ) {
            trace_ufs_err_query_attr_not_readable(idn);
            return QUERY_RESULT_NOT_READABLE;
        }
        trace_ufs_err_query_attr_not_writable(idn);
        return QUERY_RESULT_NOT_WRITEABLE;
    }

    return QUERY_RESULT_SUCCESS;
}

static QueryRespCode ufs_exec_query_flag(UfsRequest *req, int op)
{
    UfsHc *u = req->hc;
    uint8_t idn = req->req_upiu.qr.idn;
    uint32_t value;
    QueryRespCode ret;

    ret = ufs_flag_check_idn_valid(idn, op);
    if (ret) {
        return ret;
    }

    value = *(((uint8_t *)&u->flags) + idn);
    if (idn == QUERY_FLAG_IDN_FDEVICEINIT) {
        value = 0;
    } else if (op == UFS_QUERY_FLAG_READ) {
        value = *(((uint8_t *)&u->flags) + idn);
    } else if (op == UFS_QUERY_FLAG_SET) {
        value = 1;
    } else if (op == UFS_QUERY_FLAG_CLEAR) {
        value = 0;
    } else if (op == UFS_QUERY_FLAG_TOGGLE) {
        value = !value;
    } else {
        trace_ufs_err_query_invalid_opcode(op);
        return QUERY_RESULT_INVALID_OPCODE;
    }

    *(((uint8_t *)&u->flags) + idn) = value;
    req->rsp_upiu.qr.value = cpu_to_be32(value);
    return QUERY_RESULT_SUCCESS;
}

static uint32_t ufs_read_attr_value(UfsHc *u, uint8_t idn)
{
    switch (idn) {
    case QUERY_ATTR_IDN_BOOT_LU_EN:
        return u->attributes.boot_lun_en;
    case QUERY_ATTR_IDN_POWER_MODE:
        return u->attributes.current_power_mode;
    case QUERY_ATTR_IDN_ACTIVE_ICC_LVL:
        return u->attributes.active_icc_level;
    case QUERY_ATTR_IDN_OOO_DATA_EN:
        return u->attributes.out_of_order_data_en;
    case QUERY_ATTR_IDN_BKOPS_STATUS:
        return u->attributes.background_op_status;
    case QUERY_ATTR_IDN_PURGE_STATUS:
        return u->attributes.purge_status;
    case QUERY_ATTR_IDN_MAX_DATA_IN:
        return u->attributes.max_data_in_size;
    case QUERY_ATTR_IDN_MAX_DATA_OUT:
        return u->attributes.max_data_out_size;
    case QUERY_ATTR_IDN_DYN_CAP_NEEDED:
        return be32_to_cpu(u->attributes.dyn_cap_needed);
    case QUERY_ATTR_IDN_REF_CLK_FREQ:
        return u->attributes.ref_clk_freq;
    case QUERY_ATTR_IDN_CONF_DESC_LOCK:
        return u->attributes.config_descr_lock;
    case QUERY_ATTR_IDN_MAX_NUM_OF_RTT:
        return u->attributes.max_num_of_rtt;
    case QUERY_ATTR_IDN_EE_CONTROL:
        return be16_to_cpu(u->attributes.exception_event_control);
    case QUERY_ATTR_IDN_EE_STATUS:
        return be16_to_cpu(u->attributes.exception_event_status);
    case QUERY_ATTR_IDN_SECONDS_PASSED:
        return be32_to_cpu(u->attributes.seconds_passed);
    case QUERY_ATTR_IDN_CNTX_CONF:
        return be16_to_cpu(u->attributes.context_conf);
    case QUERY_ATTR_IDN_FFU_STATUS:
        return u->attributes.device_ffu_status;
    case QUERY_ATTR_IDN_PSA_STATE:
        return be32_to_cpu(u->attributes.psa_state);
    case QUERY_ATTR_IDN_PSA_DATA_SIZE:
        return u->attributes.psa_data_size;
    case QUERY_ATTR_IDN_REF_CLK_GATING_WAIT_TIME:
        return u->attributes.ref_clk_gating_wait_time;
    case QUERY_ATTR_IDN_CASE_ROUGH_TEMP:
        return u->attributes.device_case_rough_temperaure;
    case QUERY_ATTR_IDN_HIGH_TEMP_BOUND:
        return u->attributes.device_too_high_temp_boundary;
    case QUERY_ATTR_IDN_LOW_TEMP_BOUND:
        return u->attributes.device_too_low_temp_boundary;
    case QUERY_ATTR_IDN_THROTTLING_STATUS:
        return u->attributes.throttling_status;
    case QUERY_ATTR_IDN_WB_FLUSH_STATUS:
        return u->attributes.wb_buffer_flush_status;
    case QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE:
        return u->attributes.available_wb_buffer_size;
    case QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST:
        return u->attributes.wb_buffer_life_time_est;
    case QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE:
        return be32_to_cpu(u->attributes.current_wb_buffer_size);
    case QUERY_ATTR_IDN_REFRESH_STATUS:
        return u->attributes.refresh_status;
    case QUERY_ATTR_IDN_REFRESH_FREQ:
        return u->attributes.refresh_freq;
    case QUERY_ATTR_IDN_REFRESH_UNIT:
        return u->attributes.refresh_unit;
    }
    return 0;
}

static void ufs_write_attr_value(UfsHc *u, uint8_t idn, uint32_t value)
{
    switch (idn) {
    case QUERY_ATTR_IDN_ACTIVE_ICC_LVL:
        u->attributes.active_icc_level = value;
        break;
    case QUERY_ATTR_IDN_MAX_DATA_IN:
        u->attributes.max_data_in_size = value;
        break;
    case QUERY_ATTR_IDN_MAX_DATA_OUT:
        u->attributes.max_data_out_size = value;
        break;
    case QUERY_ATTR_IDN_REF_CLK_FREQ:
        u->attributes.ref_clk_freq = value;
        break;
    case QUERY_ATTR_IDN_MAX_NUM_OF_RTT:
        u->attributes.max_num_of_rtt = value;
        break;
    case QUERY_ATTR_IDN_EE_CONTROL:
        u->attributes.exception_event_control = cpu_to_be16(value);
        break;
    case QUERY_ATTR_IDN_SECONDS_PASSED:
        u->attributes.seconds_passed = cpu_to_be32(value);
        break;
    case QUERY_ATTR_IDN_PSA_STATE:
        u->attributes.psa_state = value;
        break;
    case QUERY_ATTR_IDN_PSA_DATA_SIZE:
        u->attributes.psa_data_size = cpu_to_be32(value);
        break;
    }
}

static QueryRespCode ufs_exec_query_attr(UfsRequest *req, int op)
{
    UfsHc *u = req->hc;
    uint8_t idn = req->req_upiu.qr.idn;
    uint32_t value;
    QueryRespCode ret;

    ret = ufs_attr_check_idn_valid(idn, op);
    if (ret) {
        return ret;
    }

    if (op == UFS_QUERY_ATTR_READ) {
        value = ufs_read_attr_value(u, idn);
    } else {
        value = be32_to_cpu(req->req_upiu.qr.value);
        ufs_write_attr_value(u, idn, value);
    }

    req->rsp_upiu.qr.value = cpu_to_be32(value);
    return QUERY_RESULT_SUCCESS;
}

static const RpmbUnitDescriptor rpmb_unit_desc = {
    .length = sizeof(RpmbUnitDescriptor),
    .descriptor_idn = 2,
    .unit_index = UFS_UPIU_RPMB_WLUN,
    .lu_enable = 0,
};

static QueryRespCode ufs_read_unit_desc(UfsRequest *req)
{
    UfsHc *u = req->hc;
    uint8_t lun = req->req_upiu.qr.index;

    if (lun != UFS_UPIU_RPMB_WLUN &&
        (lun > UFS_MAX_LUS || u->lus[lun] == NULL)) {
        trace_ufs_err_query_invalid_index(req->req_upiu.qr.opcode, lun);
        return QUERY_RESULT_INVALID_INDEX;
    }

    if (lun == UFS_UPIU_RPMB_WLUN) {
        memcpy(&req->rsp_upiu.qr.data, &rpmb_unit_desc, rpmb_unit_desc.length);
    } else {
        memcpy(&req->rsp_upiu.qr.data, &u->lus[lun]->unit_desc,
               sizeof(u->lus[lun]->unit_desc));
    }

    return QUERY_RESULT_SUCCESS;
}

static const StringDescriptor manufacturer_str_desc = {
    .length = 0x12,
    .descriptor_idn = QUERY_DESC_IDN_STRING,
    .UC = { 'S', 'A', 'M', 'S', 'U', 'N', 'G' },
};

static const StringDescriptor product_name_str_desc = {
    .length = 0x22,
    .descriptor_idn = QUERY_DESC_IDN_STRING,
    .UC = { 'Q', 'E', 'M', 'U', '-', 'U', 'F', 'S' },
};

static const StringDescriptor product_rev_level_str_desc = {
    .length = 0x0a,
    .descriptor_idn = QUERY_DESC_IDN_STRING,
    .UC = { '0', '0', '0', '1' },
};

static const StringDescriptor null_str_desc = {
    .length = 0x02,
    .descriptor_idn = QUERY_DESC_IDN_STRING,
};

static QueryRespCode ufs_read_string_desc(UfsRequest *req)
{
    UfsHc *u = req->hc;
    uint8_t index = req->req_upiu.qr.index;
    if (index == u->device_desc.manufacturer_name) {
        memcpy(&req->rsp_upiu.qr.data, &manufacturer_str_desc,
               manufacturer_str_desc.length);
    } else if (index == u->device_desc.product_name) {
        memcpy(&req->rsp_upiu.qr.data, &product_name_str_desc,
               product_name_str_desc.length);
    } else if (index == u->device_desc.serial_number) {
        memcpy(&req->rsp_upiu.qr.data, &null_str_desc, null_str_desc.length);
    } else if (index == u->device_desc.oem_id) {
        memcpy(&req->rsp_upiu.qr.data, &null_str_desc, null_str_desc.length);
    } else if (index == u->device_desc.product_revision_level) {
        memcpy(&req->rsp_upiu.qr.data, &product_rev_level_str_desc,
               product_rev_level_str_desc.length);
    } else {
        trace_ufs_err_query_invalid_index(req->req_upiu.qr.opcode, index);
        return QUERY_RESULT_INVALID_INDEX;
    }
    return QUERY_RESULT_SUCCESS;
}

static const InterconnectDescriptor interconnect_desc = {
    .length = sizeof(InterconnectDescriptor),
    .descriptor_idn = QUERY_DESC_IDN_INTERCONNECT,
    .bcd_unipro_version = 0x180,
    .bcd_mphy_version = 0x410,
};

static QueryRespCode ufs_read_desc(UfsRequest *req)
{
    UfsHc *u = req->hc;
    QueryRespCode status;
    uint8_t idn = req->req_upiu.qr.idn;
    uint16_t length = be16_to_cpu(req->req_upiu.qr.length);

    switch (idn) {
    case QUERY_DESC_IDN_DEVICE:
        memcpy(&req->rsp_upiu.qr.data, &u->device_desc, sizeof(u->device_desc));
        status = QUERY_RESULT_SUCCESS;
        break;
    case QUERY_DESC_IDN_UNIT:
        status = ufs_read_unit_desc(req);
        break;
    case QUERY_DESC_IDN_GEOMETRY:
        memcpy(&req->rsp_upiu.qr.data, &u->geometry_desc,
               sizeof(u->geometry_desc));
        status = QUERY_RESULT_SUCCESS;
        break;
    case QUERY_DESC_IDN_INTERCONNECT: {
        memcpy(&req->rsp_upiu.qr.data, &interconnect_desc,
               sizeof(interconnect_desc));
        status = QUERY_RESULT_SUCCESS;
        break;
    }
    case QUERY_DESC_IDN_STRING:
        status = ufs_read_string_desc(req);
        break;
    case QUERY_DESC_IDN_POWER:
        /* mocking of power descriptor is not supported */
        memset(&req->rsp_upiu.qr.data, 0, sizeof(PowerParametersDescriptor));
        req->rsp_upiu.qr.data[0] = sizeof(PowerParametersDescriptor);
        req->rsp_upiu.qr.data[1] = QUERY_DESC_IDN_POWER;
        status = QUERY_RESULT_SUCCESS;
        break;
    case QUERY_DESC_IDN_HEALTH:
        /* mocking of health descriptor is not supported */
        memset(&req->rsp_upiu.qr.data, 0, sizeof(DeviceHealthDescriptor));
        req->rsp_upiu.qr.data[0] = sizeof(DeviceHealthDescriptor);
        req->rsp_upiu.qr.data[1] = QUERY_DESC_IDN_HEALTH;
        status = QUERY_RESULT_SUCCESS;
        break;
    case QUERY_DESC_IDN_CONFIGURATION:
        /* TODO: Missing ConfigrurationDesciptor definition */
        memset(&req->rsp_upiu.qr.data, 0, 0x90);
        req->rsp_upiu.qr.data[0] = 0x90;
        req->rsp_upiu.qr.data[1] = QUERY_DESC_IDN_CONFIGURATION;
        status = QUERY_RESULT_SUCCESS;
        break;
    default:
        length = 0;
        trace_ufs_err_query_invalid_idn(req->req_upiu.qr.opcode, idn);
        status = QUERY_RESULT_INVALID_IDN;
    }

    if (length > req->rsp_upiu.qr.data[0]) {
        length = req->rsp_upiu.qr.data[0];
    }
    req->rsp_upiu.qr.opcode = req->req_upiu.qr.opcode;
    req->rsp_upiu.qr.idn = req->req_upiu.qr.idn;
    req->rsp_upiu.qr.index = req->req_upiu.qr.index;
    req->rsp_upiu.qr.selector = req->req_upiu.qr.selector;
    req->rsp_upiu.qr.length = cpu_to_be16(length);

    return status;
}

static QueryRespCode ufs_exec_query_read(UfsRequest *req)
{
    QueryRespCode status;
    switch (req->req_upiu.qr.opcode) {
    case UPIU_QUERY_OPCODE_NOP:
        status = QUERY_RESULT_SUCCESS;
        break;
    case UPIU_QUERY_OPCODE_READ_DESC:
        status = ufs_read_desc(req);
        break;
    case UPIU_QUERY_OPCODE_READ_ATTR:
        status = ufs_exec_query_attr(req, UFS_QUERY_ATTR_READ);
        break;
    case UPIU_QUERY_OPCODE_READ_FLAG:
        status = ufs_exec_query_flag(req, UFS_QUERY_FLAG_READ);
        break;
    default:
        trace_ufs_err_query_invalid_opcode(req->req_upiu.qr.opcode);
        status = QUERY_RESULT_INVALID_OPCODE;
        break;
    }

    return status;
}

static QueryRespCode ufs_exec_query_write(UfsRequest *req)
{
    QueryRespCode status;
    switch (req->req_upiu.qr.opcode) {
    case UPIU_QUERY_OPCODE_NOP:
        status = QUERY_RESULT_SUCCESS;
        break;
    case UPIU_QUERY_OPCODE_WRITE_DESC:
        /* write descriptor is not supported */
        status = QUERY_RESULT_NOT_WRITEABLE;
        break;
    case UPIU_QUERY_OPCODE_WRITE_ATTR:
        status = ufs_exec_query_attr(req, UFS_QUERY_ATTR_WRITE);
        break;
    case UPIU_QUERY_OPCODE_SET_FLAG:
        status = ufs_exec_query_flag(req, UFS_QUERY_FLAG_SET);
        break;
    case UPIU_QUERY_OPCODE_CLEAR_FLAG:
        status = ufs_exec_query_flag(req, UFS_QUERY_FLAG_CLEAR);
        break;
    case UPIU_QUERY_OPCODE_TOGGLE_FLAG:
        status = ufs_exec_query_flag(req, UFS_QUERY_FLAG_TOGGLE);
        break;
    default:
        trace_ufs_err_query_invalid_opcode(req->req_upiu.qr.opcode);
        status = QUERY_RESULT_INVALID_OPCODE;
        break;
    }

    return status;
}

static UfsReqResult ufs_exec_query_cmd(UfsRequest *req)
{
    uint8_t query_func = req->req_upiu.header.query_func;
    uint16_t data_segment_length;
    QueryRespCode status;

    trace_ufs_exec_query_cmd(req->slot, req->req_upiu.qr.opcode);
    if (query_func == UPIU_QUERY_FUNC_STANDARD_READ_REQUEST) {
        status = ufs_exec_query_read(req);
    } else if (query_func == UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST) {
        status = ufs_exec_query_write(req);
    } else {
        status = QUERY_RESULT_GENERAL_FAILURE;
    }

    data_segment_length = be16_to_cpu(req->rsp_upiu.qr.length);
    ufs_build_upiu_header(req, UPIU_TRANSACTION_QUERY_RSP, 0, status, 0,
                          data_segment_length);

    if (status != QUERY_RESULT_SUCCESS) {
        return UFS_REQUEST_ERROR;
    }
    return UFS_REQUEST_SUCCESS;
}

static void ufs_exec_req(UfsRequest *req)
{
    UfsReqResult req_result;

    if (ufs_dma_read_upiu(req)) {
        return;
    }

    switch (req->req_upiu.header.trans_type) {
    case UPIU_TRANSACTION_NOP_OUT:
        req_result = ufs_exec_nop_cmd(req);
        break;
    case UPIU_TRANSACTION_COMMAND:
        req_result = ufs_exec_scsi_cmd(req);
        break;
    case UPIU_TRANSACTION_QUERY_REQ:
        req_result = ufs_exec_query_cmd(req);
        break;
    default:
        trace_ufs_err_invalid_trans_code(req->slot,
                                         req->req_upiu.header.trans_type);
        req_result = UFS_REQUEST_ERROR;
    }

    /*
     * The ufs_complete_req for scsi commands is handled by the
     * ufs_scsi_command_complete() callback function. Therefore, to avoid
     * duplicate processing, ufs_complete_req() is not called for scsi commands.
     */
    if (req_result != UFS_REQUEST_NO_COMPLETE) {
        ufs_complete_req(req, req_result);
    }
}

static void ufs_process_req(void *opaque)
{
    UfsHc *u = opaque;
    UfsRequest *req;
    int slot;

    for (slot = 0; slot < u->params.nutrs; slot++) {
        req = &u->req_list[slot];

        if (req->state != UFS_REQUEST_READY) {
            continue;
        }
        trace_ufs_process_req(slot);
        req->state = UFS_REQUEST_RUNNING;

        ufs_exec_req(req);
    }
}

static void ufs_complete_req(UfsRequest *req, UfsReqResult req_result)
{
    UfsHc *u = req->hc;
    assert(req->state == UFS_REQUEST_RUNNING);

    if (req_result == UFS_REQUEST_SUCCESS) {
        req->utrd.header.dword_2 = OCS_SUCCESS;
    } else {
        req->utrd.header.dword_2 = OCS_INVALID_CMD_TABLE_ATTR;
    }

    trace_ufs_complete_req(req->slot);
    req->state = UFS_REQUEST_COMPLETE;
    qemu_bh_schedule(u->complete_bh);
}

static void ufs_clear_req(UfsRequest *req)
{
    if (req->sg != NULL) {
        qemu_sglist_destroy(req->sg);
        g_free(req->sg);
        req->sg = NULL;
    }

    memset(&req->utrd, 0, sizeof(req->utrd));
    memset(&req->req_upiu, 0, sizeof(req->req_upiu));
    memset(&req->rsp_upiu, 0, sizeof(req->rsp_upiu));
}

static void ufs_sendback_req(void *opaque)
{
    UfsHc *u = opaque;
    UfsRequest *req;
    int slot;

    for (slot = 0; slot < u->params.nutrs; slot++) {
        uint32_t is = ldl_le_p(&u->reg.is);
        uint32_t utrldbr = ldl_le_p(&u->reg.utrldbr);
        uint32_t utrlcnr = ldl_le_p(&u->reg.utrlcnr);

        req = &u->req_list[slot];

        if (req->state != UFS_REQUEST_COMPLETE) {
            continue;
        }

        if (ufs_dma_write_upiu(req)) {
            continue;
        }

        /*
         * TODO: UTP Transfer Request Interrupt Aggregation Control is not yet
         * supported
         */
        if (req->utrd.header.dword_2 != OCS_SUCCESS ||
            req->utrd.header.dword_0 & UTP_REQ_DESC_INT_CMD) {
            UFS_IS_SET_UTRCS(is, 1);
        }

        utrldbr &= ~(1 << slot);
        utrlcnr |= (1 << slot);

        stl_le_p(&u->reg.is, is);
        stl_le_p(&u->reg.utrldbr, utrldbr);
        stl_le_p(&u->reg.utrlcnr, utrlcnr);

        trace_ufs_sendback_req(req->slot);

        ufs_clear_req(req);
        req->state = UFS_REQUEST_IDLE;
    }

    ufs_irq_check(u);
}

static bool ufs_check_constraints(UfsHc *u, Error **errp)
{
    if (u->params.nutrs > UFS_MAX_NUTRS) {
        error_setg(errp, "nutrs must be less than %d", UFS_MAX_NUTRS);
        return false;
    }

    if (u->params.nutmrs > UFS_MAX_NUTMRS) {
        error_setg(errp, "nutmrs must be less than %d", UFS_MAX_NUTMRS);
        return false;
    }

    return true;
}

static void ufs_init_pci(UfsHc *u, PCIDevice *pci_dev)
{
    uint8_t *pci_conf = pci_dev->config;

    pci_conf[PCI_INTERRUPT_PIN] = 1;
    pci_config_set_prog_interface(pci_conf, 0x1);

    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_REDHAT);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_REDHAT_UFS);

    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_UFS);

    memory_region_init_io(&u->iomem, OBJECT(u), &ufs_mmio_ops, u, "ufs",
                          u->reg_size);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &u->iomem);
    u->irq = pci_allocate_irq(pci_dev);
}

static void ufs_init_state(UfsHc *u)
{
    u->req_list = g_new0(UfsRequest, u->params.nutrs);

    for (int i = 0; i < u->params.nutrs; i++) {
        u->req_list[i].hc = u;
        u->req_list[i].slot = i;
        u->req_list[i].sg = NULL;
        u->req_list[i].state = UFS_REQUEST_IDLE;
    }

    u->doorbell_bh = qemu_bh_new(ufs_process_req, u);
    u->complete_bh = qemu_bh_new(ufs_sendback_req, u);
}

static void ufs_init_hc(UfsHc *u)
{
    uint32_t cap = 0;

    u->reg_size = pow2ceil(sizeof(UfsReg));

    memset(&u->reg, 0, sizeof(u->reg));
    UFS_CAP_SET_NUTRS(cap, (u->params.nutrs - 1));
    UFS_CAP_SET_RTT(cap, 2);
    UFS_CAP_SET_NUTMRS(cap, (u->params.nutmrs - 1));
    UFS_CAP_SET_AUTOH8(cap, 0);
    UFS_CAP_SET_64AS(cap, 1);
    UFS_CAP_SET_OODDS(cap, 0);
    UFS_CAP_SET_UICDMETMS(cap, 0);
    UFS_CAP_SET_CS(cap, 0);
    stl_le_p(&u->reg.cap, cap);
    stl_le_p(&u->reg.ver, UFS_SPEC_VER);

    memset(&u->device_desc, 0, sizeof(DeviceDescriptor));
    u->device_desc.length = sizeof(DeviceDescriptor);
    u->device_desc.descriptor_idn = QUERY_DESC_IDN_DEVICE;
    u->device_desc.device_sub_class = 0x01;
    u->device_desc.number_lu = 0x00;
    u->device_desc.number_wlu = 0x04;
    /* TODO: Revisit it when Power Management is implemented */
    u->device_desc.init_power_mode = 0x01; /* Active Mode */
    u->device_desc.high_priority_lun = 0x7F; /* Same Priority */
    u->device_desc.spec_version = cpu_to_be16(0x0310);
    u->device_desc.manufacturer_name = 0x00;
    u->device_desc.product_name = 0x01;
    u->device_desc.serial_number = 0x02;
    u->device_desc.oem_id = 0x03;
    u->device_desc.ud_0_base_offset = 0x16;
    u->device_desc.ud_config_p_length = 0x1A;
    u->device_desc.device_rtt_cap = 0x02;
    u->device_desc.queue_depth = u->params.nutrs;
    u->device_desc.product_revision_level = 0x04;
    u->device_desc.extended_ufs_features_support = 0x00;

    memset(&u->geometry_desc, 0, sizeof(GeometryDescriptor));
    u->geometry_desc.length = sizeof(GeometryDescriptor);
    u->geometry_desc.descriptor_idn = QUERY_DESC_IDN_GEOMETRY;
    u->geometry_desc.total_raw_device_capacity = 0;
    u->geometry_desc.max_number_lu = (UFS_MAX_LUS == 32) ? 0x1 : 0x0;
    u->geometry_desc.segment_size = cpu_to_be32(0x2000); /* 4KB */
    u->geometry_desc.allocation_unit_size = 0x1; /* 4KB */
    u->geometry_desc.min_addr_block_size = 0x8; /* 4KB */
    u->geometry_desc.max_in_buffer_size = 0x8;
    u->geometry_desc.max_out_buffer_size = 0x8;
    u->geometry_desc.rpmb_read_write_size = 0x40;
    u->geometry_desc.data_ordering =
        0x0; /* out-of-order data transfer is not supported */
    u->geometry_desc.max_context_id_number = 0x5;
    u->geometry_desc.supported_memory_types = cpu_to_be16(0x8001);

    memset(&u->attributes, 0, sizeof(u->attributes));
    u->attributes.max_data_in_size = 0x08;
    u->attributes.max_data_out_size = 0x08;
    u->attributes.ref_clk_freq = 0x01; /* 26 MHz */
    /* configure descriptor is not supported */
    u->attributes.config_descr_lock = 0x01;
    u->attributes.max_num_of_rtt = 0x02;

    memset(&u->flags, 0, sizeof(u->flags));
    u->flags.permanently_disable_fw_update = 1;
}

static bool ufs_init_wlu(UfsHc *u, UfsWLu **wlu, uint8_t wlun, Error **errp)
{
    UfsWLu *new_wlu = UFSWLU(qdev_new(TYPE_UFS_WLU));

    qdev_prop_set_uint32(DEVICE(new_wlu), "lun", wlun);

    /*
     * The well-known lu shares the same bus as the normal lu. If the well-known
     * lu writes the same channel value as the normal lu, the report will be
     * made not only for the normal lu but also for the well-known lu at
     * REPORT_LUN time. To prevent this, the channel value of normal lu is fixed
     * to 0 and the channel value of well-known lu is fixed to 1.
     */
    qdev_prop_set_uint32(DEVICE(new_wlu), "channel", 1);
    if (!qdev_realize_and_unref(DEVICE(new_wlu), BUS(&u->bus), errp)) {
        return false;
    }

    *wlu = new_wlu;
    return true;
}

static void ufs_realize(PCIDevice *pci_dev, Error **errp)
{
    UfsHc *u = UFS(pci_dev);

    if (!ufs_check_constraints(u, errp)) {
        return;
    }

    qbus_init(&u->bus, sizeof(UfsBus), TYPE_UFS_BUS, &pci_dev->qdev,
              u->parent_obj.qdev.id);
    u->bus.parent_bus.info = &ufs_scsi_info;

    ufs_init_state(u);
    ufs_init_hc(u);
    ufs_init_pci(u, pci_dev);

    if (!ufs_init_wlu(u, &u->report_wlu, UFS_UPIU_REPORT_LUNS_WLUN, errp)) {
        return;
    }

    if (!ufs_init_wlu(u, &u->dev_wlu, UFS_UPIU_UFS_DEVICE_WLUN, errp)) {
        return;
    }

    if (!ufs_init_wlu(u, &u->boot_wlu, UFS_UPIU_BOOT_WLUN, errp)) {
        return;
    }

    if (!ufs_init_wlu(u, &u->rpmb_wlu, UFS_UPIU_RPMB_WLUN, errp)) {
        return;
    }
}

static void ufs_exit(PCIDevice *pci_dev)
{
    UfsHc *u = UFS(pci_dev);

    if (u->dev_wlu) {
        object_unref(OBJECT(u->dev_wlu));
        u->dev_wlu = NULL;
    }

    if (u->report_wlu) {
        object_unref(OBJECT(u->report_wlu));
        u->report_wlu = NULL;
    }

    if (u->rpmb_wlu) {
        object_unref(OBJECT(u->rpmb_wlu));
        u->rpmb_wlu = NULL;
    }

    if (u->boot_wlu) {
        object_unref(OBJECT(u->boot_wlu));
        u->boot_wlu = NULL;
    }

    qemu_bh_delete(u->doorbell_bh);
    qemu_bh_delete(u->complete_bh);

    for (int i = 0; i < u->params.nutrs; i++) {
        ufs_clear_req(&u->req_list[i]);
    }
    g_free(u->req_list);
}

static Property ufs_props[] = {
    DEFINE_PROP_STRING("serial", UfsHc, params.serial),
    DEFINE_PROP_UINT8("nutrs", UfsHc, params.nutrs, 32),
    DEFINE_PROP_UINT8("nutmrs", UfsHc, params.nutmrs, 8),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription ufs_vmstate = {
    .name = "ufs",
    .unmigratable = 1,
};

static void ufs_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = ufs_realize;
    pc->exit = ufs_exit;
    pc->class_id = PCI_CLASS_STORAGE_UFS;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = "Universal Flash Storage";
    device_class_set_props(dc, ufs_props);
    dc->vmsd = &ufs_vmstate;
}

static bool ufs_bus_check_address(BusState *qbus, DeviceState *qdev,
                                  Error **errp)
{
    SCSIDevice *dev = SCSI_DEVICE(qdev);
    UfsBusClass *ubc = UFS_BUS_GET_CLASS(qbus);
    UfsHc *u = UFS(qbus->parent);

    if (strcmp(object_get_typename(OBJECT(dev)), TYPE_UFS_WLU) == 0) {
        if (dev->lun != UFS_UPIU_REPORT_LUNS_WLUN &&
            dev->lun != UFS_UPIU_UFS_DEVICE_WLUN &&
            dev->lun != UFS_UPIU_BOOT_WLUN && dev->lun != UFS_UPIU_RPMB_WLUN) {
            error_setg(errp, "bad well-known lun: %d", dev->lun);
            return false;
        }

        if ((dev->lun == UFS_UPIU_REPORT_LUNS_WLUN && u->report_wlu != NULL) ||
            (dev->lun == UFS_UPIU_UFS_DEVICE_WLUN && u->dev_wlu != NULL) ||
            (dev->lun == UFS_UPIU_BOOT_WLUN && u->boot_wlu != NULL) ||
            (dev->lun == UFS_UPIU_RPMB_WLUN && u->rpmb_wlu != NULL)) {
            error_setg(errp, "well-known lun %d already exists", dev->lun);
            return false;
        }

        return true;
    }

    if (strcmp(object_get_typename(OBJECT(dev)), TYPE_UFS_LU) != 0) {
        error_setg(errp, "%s cannot be connected to ufs-bus",
                   object_get_typename(OBJECT(dev)));
        return false;
    }

    return ubc->parent_check_address(qbus, qdev, errp);
}

static void ufs_bus_class_init(ObjectClass *class, void *data)
{
    BusClass *bc = BUS_CLASS(class);
    UfsBusClass *ubc = UFS_BUS_CLASS(class);
    ubc->parent_check_address = bc->check_address;
    bc->check_address = ufs_bus_check_address;
}

static const TypeInfo ufs_info = {
    .name = TYPE_UFS,
    .parent = TYPE_PCI_DEVICE,
    .class_init = ufs_class_init,
    .instance_size = sizeof(UfsHc),
    .interfaces = (InterfaceInfo[]){ { INTERFACE_PCIE_DEVICE }, {} },
};

static const TypeInfo ufs_bus_info = {
    .name = TYPE_UFS_BUS,
    .parent = TYPE_SCSI_BUS,
    .class_init = ufs_bus_class_init,
    .class_size = sizeof(UfsBusClass),
    .instance_size = sizeof(UfsBus),
};

static void ufs_register_types(void)
{
    type_register_static(&ufs_info);
    type_register_static(&ufs_bus_info);
}

type_init(ufs_register_types)