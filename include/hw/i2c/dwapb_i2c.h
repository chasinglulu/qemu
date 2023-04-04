/*
 *  DWAPB I2C Bus Serial Interface registers definition
 *
 *  Copyright (C) 2023 xinlu wang
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef DWAPB_I2C_H
#define DWAPB_I2C_H

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "qom/object.h"

#define TYPE_DWAPB_I2C                  "dwapb-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(DWAPBI2CState, DWAPB_I2C)

/* DWAPB I2C memory map */
#define DWAPB_I2C_MEM_SIZE              0x200
#define I2C_CON_ADDR                       0x00  /* control register */
#define I2C_TAR_ADDR                      0x04  /* control/status register */
#define I2C_SAR_ADDR                       0x08  /* address register */
#define I2CDS_ADDR                        0x0c  /* data shift register */
#define I2CLC_ADDR                        0x10  /* line control register */

#define I2CCON_ACK_GEN                    (1 << 7)
#define I2CCON_INTRS_EN                   (1 << 5)
#define I2CCON_INT_PEND                   (1 << 4)

#define DWAPB_I2C_MODE(reg)             (((reg) >> 6) & 3)
#define I2C_IN_MASTER_MODE(reg)           (((reg) >> 6) & 2)
#define I2CMODE_MASTER_Rx                 0x2
#define I2CMODE_MASTER_Tx                 0x3
#define I2CSTAT_LAST_BIT                  (1 << 0)
#define I2CSTAT_OUTPUT_EN                 (1 << 4)
#define I2CSTAT_START_BUSY                (1 << 5)

struct DWAPBI2CState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;

    uint32_t i2c_con;
    uint32_t i2c_tar;
    uint32_t i2c_sar;
    uint32_t i2c_hs_maddr;
    uint32_t i2c_data_cmd;
    uint32_t i2c_intr_stat;
    uint32_t i2c_intr_mask;
    uint32_t i2c_raw_intr_stat;
    uint32_t i2c_rx_tl;
    uint32_t i2c_tx_tl;
    uint32_t i2c_clr_intr;
    uint32_t i2c_clr_rx_under;
    uint32_t i2c_clr_rx_over;
    uint32_t i2c_clr_tx_over;
    uint32_t i2c_clr_rd_req;
    uint32_t i2c_clr_tx_abrt;
    uint32_t i2c_clr_rx_done;
    uint32_t i2c_clr_activity;
    uint32_t i2c_clr_stop_det;
    uint32_t i2c_clr_start_det;
    uint32_t i2c_clr_gen_call;
    uint32_t i2c_enable;
    uint32_t i2c_status;
    uint32_t i2c_txflr;
    uint32_t i2c_rxflr;
    uint32_t i2c_sda_hold;
    uint32_t i2c_tx_abrt_source;
    uint32_t i2c_slv_data_nack_only;
    uint32_t i2c_dma_cr;
    uint32_t i2c_dma_tdlr;
    uint32_t i2c_dma_rdlr;
    uint32_t i2c_sda_setup;
    uint32_t i2c_ack_general_call;
    uint32_t i2c_enable_status;
};
#endif