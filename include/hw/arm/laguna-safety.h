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
#include "hw/char/dwc-apb-uart.h"
#include "qemu/log.h"
#include "exec/hwaddr.h"
#include "target/arm/cpu.h"
#include "hw/sd/sdhci.h"
#include "hw/net/dwc_eth_qos.h"
#include "hw/ssi/designware_spi.h"
#include "hw/timer/cadence_ttc.h"
#include "hw/sd/sdhci.h"

#define TYPE_LUA_SAFETY "laguna-safety-island"
OBJECT_DECLARE_SIMPLE_TYPE(LagunaSafety, LUA_SAFETY)

#define LUA_SAFETY_MCPU_TYPE ARM_CPU_TYPE_NAME("cortex-r5f")

#define LUA_SAFETY_NR_MCPUS			2
#define LUA_SAFETY_NR_MPU_UARTS		4
#define LUA_SAFETY_NR_SDHCI			2
#define LUA_SAFETY_NR_GPIO			2
#define LUA_SAFETY_NR_TIMER			8
#define LUA_SAFETY_NUM_IRQS			480

#define ARCH_VITRUAL_PMU_IRQ		7
#define ARCH_GIC_MAINT_IRQ			9
#define ARCH_TIMER_VIRT_IRQ			11
#define ARCH_TIMER_S_EL1_IRQ		13
#define ARCH_TIMER_NS_EL1_IRQ		14
#define ARCH_TIMER_NS_EL2_IRQ		10

enum {
	VIRT_TCM,
	VIRT_OCM,
	VIRT_IRAM,
	VIRT_CORE0_TCM_SLAVE,
	VIRT_CORE1_TCM_SLAVE,
	VIRT_GIC1_DIST,
	VIRT_GIC1_CPU,
	VIRT_GIC2_DIST,
	VIRT_GIC2_CPU,
	VIRT_QSPI,
	VIRT_EMAC,
	VIRT_UART,
	VIRT_TIMER,
	VIRT_FLASH_EMMC,
	VIRT_FLASH_OSPI,
	VIRT_BOOTROM,
};

static const MemMapEntry base_memmap[] = {
	[VIRT_TCM]               =    { 0x00000000, 0x00008000 },
	[VIRT_OCM]               =    { 0x00200000, 0x00200000 },
	[VIRT_IRAM]              =    { 0x00400000, 0x00010000 },
	[VIRT_CORE0_TCM_SLAVE]   =    { 0x00440000, 0x00040000 },
	[VIRT_CORE1_TCM_SLAVE]   =    { 0x00480000, 0x00040000 },
	[VIRT_GIC1_DIST]         =    { 0x00501000, 0x00001000 },
	[VIRT_GIC1_CPU]          =    { 0x00502000, 0x00002000 },
	[VIRT_GIC2_DIST]         =    { 0x00509000, 0x00001000 },
	[VIRT_GIC2_CPU]          =    { 0x0050A000, 0x00002000 },
	[VIRT_QSPI]              =    { 0x0051F000, 0x00001000 },
	[VIRT_EMAC]              =    { 0x00534000, 0x00004000 },
	[VIRT_UART]              =    { 0x00602000, 0x00001000 },
	[VIRT_TIMER]             =    { 0x006C7000, 0x00001000 },
	[VIRT_FLASH_EMMC]        =    { 0x0C010000, 0x00002000 },
	[VIRT_FLASH_OSPI]        =    { 0x0C040000, 0x00001000 },
	[VIRT_BOOTROM]           =    { 0xFFFF0000, 0x00010000 },
};

static const MemMapEntry unimp_memmap[] = {
	{ 0x00600000, 0x00001000 },
};

static const int mpu_irqmap[] = {
	[VIRT_UART] = 14,	/* ...to 14 + LUA_SAFETY_NR_APU_UARTS - 1 */
	[VIRT_EMAC] = 45,
	[VIRT_TIMER] = 100,
	[VIRT_QSPI] = 44,
};

struct LagunaSafety {
	/*< private >*/
	SysBusDevice parent_obj;

	/*< public >*/
	struct {
		struct {
			DWCUARTState uarts[LUA_SAFETY_NR_MPU_UARTS];
			CadenceTTCState ttc[LUA_SAFETY_NR_TIMER];
			SDHCIState mmc[LUA_SAFETY_NR_SDHCI];
			DesignwareEtherQoSState eqos;
			DWCSPIState qspi;
			DWCSPIState ospi;
		} peri;

		ARMCPU cpus[LUA_SAFETY_NR_MCPUS];
		GICState gic;
	} mpu;

	MemoryRegion mr_ocm;
	MemoryRegion mr_iram;
	MemoryRegion mr_tcm[LUA_SAFETY_NR_MCPUS * 2];
	MemoryRegion mr_tcm_slv[LUA_SAFETY_NR_MCPUS * 2];
	MemoryRegion mr_cpu[LUA_SAFETY_NR_MCPUS];
	MemoryRegion mr_cpu_alias[LUA_SAFETY_NR_MCPUS];

	struct {
		bool lockstep;
		char *nor_flash;
	} cfg;
};
#endif