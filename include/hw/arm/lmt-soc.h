/*
 * Lambert SoC emulation
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

#ifndef LMT_SOC_H
#define LMT_SOC_H

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
#include "hw/net/dwc_eth_qos.h"
#include "hw/remote-port.h"
#include "hw/remote-port-vmctrl.h"

#define TYPE_LMT_SOC "lambert-soc"
OBJECT_DECLARE_SIMPLE_TYPE(LambertSoC, LMT_SOC)

#define LMT_SOC_ACPU_TYPE ARM_CPU_TYPE_NAME("cortex-a76")

#define LMT_SOC_CLUSTER_SIZE		4
#define LMT_SOC_CLUSTERS			2
#define LMT_SOC_NR_ACPUS			8
#define LMT_SOC_NR_APU_UARTS		2
#define LMT_SOC_NR_RPU_UARTS		2
#define LMT_SOC_NR_SDHCI			2
#define LMT_SOC_NR_GPIO				2
#define LMT_SOC_NUM_IRQS			480

#define ARCH_VITRUAL_PMU_IRQ		7
#define ARCH_GIC_MAINT_IRQ			9
#define ARCH_TIMER_VIRT_IRQ			11
#define ARCH_TIMER_S_EL1_IRQ		13
#define ARCH_TIMER_NS_EL1_IRQ		14
#define ARCH_TIMER_NS_EL2_IRQ		10

enum {
	VIRT_IRAM,
	VIRT_GIC_DIST,
	VIRT_GIC_CPU,
	VIRT_GIC_HYP,
	VIRT_GIC_VCPU,
	VIRT_BOOTROM,
	VIRT_EMAC,
	VIRT_UART,
	VIRT_IRAM_SAFETY,
	VIRT_VMCTRL,
	VIRT_SDHCI,
	VIRT_GPIO,
	VIRT_PMU,
	VIRT_MEM,
	VIRT_LOWMEMMAP_LAST,
};

static const MemMapEntry base_memmap[] = {
	[VIRT_IRAM] = 				{ 0x00000000, 0x00020000 },
	/* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
	[VIRT_GIC_DIST] =			{ 0x00449000, 0x00001000 },
	[VIRT_GIC_CPU] =			{ 0x0044a000, 0x00002000 },
	[VIRT_GIC_HYP] =			{ 0x0044c000, 0x00002000 },
	[VIRT_GIC_VCPU] =			{ 0x0044e000, 0x00002000 },
	[VIRT_BOOTROM] =			{ 0x10600000, 0x00010000 },
	[VIRT_UART] =				{ 0x1068a000, 0x00001000 },
	[VIRT_EMAC] =				{ 0x60824000, 0x00004000 },
	[VIRT_IRAM_SAFETY] =		{ 0x60c00000, 0x00080000 },
	[VIRT_VMCTRL] =				{ 0x60d24000, 0x00001000 },
	[VIRT_MEM] =				{ 0x400000000UL, (48UL * GiB) },
};

static const int a76irqmap[] = {
	[VIRT_EMAC] = 112,
	[VIRT_UART] = 14,	/* ...to 73 + LMT_SOC_NR_APU_UARTS - 1 */
	[VIRT_SDHCI] = 120,	/* ... 122 for SDHCI1 */
	[VIRT_GPIO] = 78,	/* ...to 78 + LMT_SOC_NR_GPIO - 1*/
};

struct LambertSoC {
	/*< private >*/
	SysBusDevice parent_obj;

	/*< public >*/
	struct {
		struct {
			DWUARTState uarts[LMT_SOC_NR_APU_UARTS];
			DesignwareEtherQoSState eqos;
		} peri;

		ARMCPU cpus[LMT_SOC_NR_ACPUS];
		GICState gic;

		RemotePort rp;
		RemotePortVMCtrl riscv_ctrl;
	} apu;

	MemoryRegion mr_ddr;
	MemoryRegion mr_iram;
	MemoryRegion mr_iram_safety;

	struct {
		MemoryRegion *mr_ddr;
		bool has_emmc;
		bool virt;
		bool secure;
		char *cpu_type;
		char *riscv_memdev;
		char *chardev_id;
	} cfg;
};

static inline uint64_t lmt_cpu_mp_affinity(int idx)
{
	uint64_t mp_aff = arm_cpu_mp_affinity(idx, LMT_SOC_CLUSTER_SIZE);

	mp_aff <<= 8;
	return mp_aff;
}

#endif