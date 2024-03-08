/*
 * Laguna Safety Island emulation
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

#ifndef LUA_SAFETY_H
#define LUA_SAFETY_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/units.h"
#include "hw/arm/boot.h"
#include "hw/intc/arm_gic.h"
#include "qom/object.h"
#include "hw/char/dw_uart.h"
#include "qemu/log.h"
#include "exec/hwaddr.h"
#include "target/arm/cpu.h"
#include "hw/sd/sdhci.h"
#include "hw/timer/cadence_ttc.h"

#define TYPE_LUA_SAFETY "laguna-safety-island"
OBJECT_DECLARE_SIMPLE_TYPE(LagunaSafety, LUA_SAFETY)

#define LUA_SAFETY_MCPU_TYPE ARM_CPU_TYPE_NAME("cortex-r5f")

#define LUA_SAFETY_NR_MCPUS			2
#define LUA_SAFETY_NR_MPU_UARTS		4
#define LUA_SAFETY_NR_SDHCI			2
#define LUA_SAFETY_NR_GPIO			2
#define LUA_SAFETY_NR_TIMER			4
#define LUA_SAFETY_NUM_IRQS			480

#define ARCH_VITRUAL_PMU_IRQ		7
#define ARCH_GIC_MAINT_IRQ			9
#define ARCH_TIMER_VIRT_IRQ			11
#define ARCH_TIMER_S_EL1_IRQ		13
#define ARCH_TIMER_NS_EL1_IRQ		14
#define ARCH_TIMER_NS_EL2_IRQ		10

enum {
	VIRT_BOOTROM,
	VIRT_EMAC,
	VIRT_UART,
	VIRT_TIMER,
	VIRT_OCM,
	VIRT_IRAM,
	VIRT_GIC_DIST,
	VIRT_GIC_CPU,
};

static const MemMapEntry base_memmap[] = {
	[VIRT_BOOTROM]           =    { 0xFFFF0000, 0x00010000 },
	[VIRT_EMAC]              =    { 0x00536000, 0x00004000 },
	[VIRT_UART]              =    { 0x00602000, 0x00001000 },
	[VIRT_TIMER]             =    { 0x00657000, 0x00001000 },
	[VIRT_OCM]               =    { 0x00200000, 0x00200000 },
	[VIRT_IRAM]              =    { 0x00400000, 0x00010000 },
	/* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
	[VIRT_GIC_DIST]          =    { 0x00501000, 0x00001000 },
	[VIRT_GIC_CPU]           =    { 0x00502000, 0x00002000 },
};

static const int mpu_irqmap[] = {
	[VIRT_UART] = 14,	/* ...to 14 + LUA_SAFETY_NR_APU_UARTS - 1 */
	[VIRT_EMAC] = 112,
};

struct LagunaSafety {
	/*< private >*/
	SysBusDevice parent_obj;

	/*< public >*/
	struct {
		struct {
			DWUARTState uarts[LUA_SAFETY_NR_MPU_UARTS];
			CadenceTTCState ttc[LUA_SAFETY_NR_TIMER];
		} peri;

		ARMCPU cpus[LUA_SAFETY_NR_MCPUS];
		GICState gic;
	} mpu;

	MemoryRegion mr_ocm;
	MemoryRegion mr_iram;
};
#endif