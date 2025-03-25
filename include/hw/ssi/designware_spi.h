/*
 * QEMU model of the DesignWare SPI Controller
 *
 * Copyright (C) 2024 Charleye <wangkart@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_DWC_SPI_H
#define HW_DWC_SPI_H
#include "qemu/fifo32.h"

#define DWC_SPI_REG_NUM  (0x100 / 4)

#define TYPE_DWC_SPI "designware.spi"
#define DWC_SPI(obj) OBJECT_CHECK(DWCSPIState, (obj), TYPE_DWC_SPI)

typedef struct DWCSPIState {
	SysBusDevice parent_obj;
	uint64_t *flash_dev;
	uint32_t flash_dev_num;
	MemoryRegion mmio;
	QEMUTimer *transfer_timer;
	qemu_irq irq;

	uint32_t num_cs;
	qemu_irq *cs_lines;
	uint32_t fifo_depth;
	uint8_t spi_mode;

	SSIBus *spi;

	Fifo32 tx_fifo;
	Fifo32 rx_fifo;

	uint32_t regs[DWC_SPI_REG_NUM];
} DWCSPIState;

#endif /* HW_DWC_SPI_H */
