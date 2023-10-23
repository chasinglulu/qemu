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
#include "qemu/log.h"
#include "exec/hwaddr.h"

#define TYPE_LMT_SAFETY "lambert-safety"
OBJECT_DECLARE_SIMPLE_TYPE(LambertSafety, LMT_SAFETY)

#define LMT_SAFETY_NR_RISCVS        2
#define LMT_SAFETY_NR_UARTS         2

enum {
	VIRT_IRAM,
	VIRT_GIC_DIST,
	VIRT_GIC_CPU,
	VIRT_GIC_HYP,
	VIRT_GIC_VCPU,
	VIRT_BOOTROM,
	VIRT_UART,
	VIRT_IRAM_SAFETY,
	VIRT_SDHCI,
	VIRT_GPIO,
	VIRT_PMU,
	VIRT_MEM,
	VIRT_LOWMEMMAP_LAST,
};

static const MemMapEntry base_memmap[] = {
	[VIRT_IRAM] =               { 0x00000000, 0x00020000 },
};

struct LambertSafety {
	/*< private >*/
	SysBusDevice parent_obj;

	/*< public >*/
	struct {
		struct {
			DWUARTState uarts[LMT_SAFETY_NR_UARTS];
		} peri;

		RISCVCPU cpus[LMT_SAFETY_NR_RISCVS];
		RISCVCLICState clic;
	} safety;

	MemoryRegion mr_iram;

	struct {
		char *memdev;
	} cfg;
};

#endif