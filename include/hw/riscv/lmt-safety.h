/*
 * Lambert safety island emulation
 *
 * Copyright (C) 2023 Charleye <wangkart@aliyun.com>
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
 *
 */

#ifndef LMT_SAFETY_H
#define LMT_SAFETY_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/units.h"
#include "hw/riscv/boot.h"
#include "qom/object.h"
#include "hw/char/dw_uart.h"
#include "hw/intc/riscv_clic.h"
#include "hw/intc/riscv_aclint.h"
#include "qemu/log.h"
#include "exec/hwaddr.h"

#define TYPE_LMT_SAFETY "lambert-safety"
OBJECT_DECLARE_SIMPLE_TYPE(LambertSafety, LMT_SAFETY)

#define LMT_SAFETY_NR_RISCVS        2
#define LMT_SAFETY_NR_UARTS         2

#define LMT_SAFETY_IRQS_NUM         256

enum {
	VIRT_IRAM,
	VIRT_CLINT,
	VIRT_CLIC,
	VIRT_UART,
	VIRT_MEM,
	VIRT_END,
};

static const MemMapEntry base_memmap[] = {
	[VIRT_IRAM] =               { 0x60c00000, 0x00080000 },
	[VIRT_CLINT] =              { 0x60c80000, 0x00080000 },
	[VIRT_CLIC] =               { 0x60d24000, 0x00001000 },
	[VIRT_UART] =               { 0x60d42000, 0x00001000 },
	[VIRT_MEM] =                { 0x60e00000, 0x00400000 },
};

static const int irqmap[] = {
	[VIRT_UART] = 0x10,	/* ...to 16 + LMT_SAFETY_NR_UARTS - 1 */
};

struct LambertSafety {
	/*< private >*/
	SysBusDevice parent_obj;

	/*< public >*/
	struct {
		struct {
			DWUARTState uarts[LMT_SAFETY_NR_UARTS];
		} peri;

		RISCVHartArrayState cpus;
		RISCVCLICState clic;
	} safety;

	MemoryRegion mr_mem;
	MemoryRegion mr_iram;

	struct {
		MemoryRegion *mr_ddr;
		char *cpu_type;
		uint32_t num_harts;
		char *memdev;
	} cfg;
};

#endif