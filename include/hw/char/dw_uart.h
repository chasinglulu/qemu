/*
 * DW UART emulation
 *
 * Copyright (C) 2023 Charleye <wangkart@aliyun.com>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_DW_SERIAL_H
#define HW_DW_SERIAL_H

#include "hw/char/serial.h"

#define DW_UART_REG_SIZE  0xE0
#define DW_UART_NUM_REGS  (DW_UART_REG_SIZE / sizeof(uint32_t))

typedef struct DWUARTState {
	SysBusDevice parent;

	MemoryRegion container;
	MemoryRegion iomem;

	uint32_t regs[DW_UART_NUM_REGS];
	uint8_t index;

	SerialMM uart;
} DWUARTState;

#define TYPE_DW_UART  "dw_uart"
#define DW_UART(obj)  OBJECT_CHECK(DWUARTState, (obj), TYPE_DW_UART)

#endif