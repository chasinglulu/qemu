/*
 * Laguna SoC emulation
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

#ifndef LUA_SOC_H
#define LUA_SOC_H

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

#define TYPE_LUA_SOC "laguna-soc"
OBJECT_DECLARE_SIMPLE_TYPE(LagunaSoC, LUA_SOC)

#define LUA_SOC_ACPU_TYPE ARM_CPU_TYPE_NAME("cortex-a55")

#define LUA_SOC_CLUSTER_SIZE		4
#define LUA_SOC_CLUSTERS			1
#define LUA_SOC_NR_ACPUS			4
#define LUA_SOC_NR_APU_UARTS		4
#define LUA_SOC_NR_SDHCI			2
#define LUA_SOC_NR_GPIO				2
#define LUA_SOC_NUM_IRQS			480

#define ARCH_VITRUAL_PMU_IRQ		7
#define ARCH_GIC_MAINT_IRQ			9
#define ARCH_TIMER_VIRT_IRQ			11
#define ARCH_TIMER_S_EL1_IRQ		13
#define ARCH_TIMER_NS_EL1_IRQ		14
#define ARCH_TIMER_NS_EL2_IRQ		10

enum {
	VIRT_BOOTROM_SAFETY,
	VIRT_OCM_SAFETY,
	VIRT_IRAM_SAFETY,
	VIRT_GIC_DIST,
	VIRT_GIC_CPU,
	VIRT_GIC_HYP,
	VIRT_GIC_VCPU,
	VIRT_EMMC,
	VIRT_EMAC,
	VIRT_UART,
	VIRT_MEM,
};

static const MemMapEntry base_memmap[] = {
	[VIRT_BOOTROM_SAFETY]    =    { 0x00000000, 0x00010000 },
	[VIRT_OCM_SAFETY]        =    { 0x00200000, 0x00200000 },
	[VIRT_IRAM_SAFETY]       =    { 0x00400000, 0x00010000 },
	/* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
	[VIRT_GIC_DIST]          =    { 0x00E01000, 0x00001000 },
	[VIRT_GIC_CPU]           =    { 0x00E02000, 0x00002000 },
	[VIRT_GIC_HYP]           =    { 0x00E04000, 0x00002000 },
	[VIRT_GIC_VCPU]          =    { 0x00E06000, 0x00002000 },
	[VIRT_EMMC]              =    { 0x01410000, 0x00001000 },
	[VIRT_EMAC]              =    { 0x01614000, 0x00004000 },
	[VIRT_UART]              =    { 0x01802000, 0x00001000 },
	[VIRT_MEM]               =    { 0x400000000UL, (48UL * GiB) },
};

static const int apu_irqmap[] = {
	[VIRT_EMMC] = 9,
	[VIRT_UART] = 14,	/* ...to 14 + LUA_SOC_NR_APU_UARTS - 1 */
	[VIRT_EMAC] = 112,
};

struct LagunaSoC {
	/*< private >*/
	SysBusDevice parent_obj;

	/*< public >*/
	struct {
		struct {
			DWUARTState uarts[LUA_SOC_NR_APU_UARTS];
			SDHCIState mmc[LUA_SOC_NR_SDHCI];
		} peri;

		ARMCPU cpus[LUA_SOC_NR_ACPUS];
		GICState gic;
	} apu;

	MemoryRegion mr_ddr;
	MemoryRegion mr_ocm_safety;
	MemoryRegion mr_iram_safety;

	struct {
		MemoryRegion *mr_ddr;
		bool has_emmc;
		uint8_t part_config;
		bool virt;
		bool secure;
	} cfg;
};

static inline uint64_t lua_cpu_mp_affinity(int idx)
{
	uint64_t mp_aff = arm_cpu_mp_affinity(idx, LUA_SOC_CLUSTER_SIZE);

	mp_aff <<= 8;
	return mp_aff;
}

#endif