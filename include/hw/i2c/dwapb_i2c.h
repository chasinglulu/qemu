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
#define DWAPB_I2C_MEM_SIZE              0x14
#define I2CCON_ADDR                       0x00  /* control register */
#define I2CSTAT_ADDR                      0x04  /* control/status register */
#define I2CADD_ADDR                       0x08  /* address register */
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

    uint8_t i2ccon;
    uint8_t i2cstat;
    uint8_t i2cadd;
    uint8_t i2cds;
    uint8_t i2clc;
    bool scl_free;
};
#endif