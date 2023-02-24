/*
 * Horizon Robotics Jounery SoC emulation
 *
 * Copyright (C) 2023 Horizon Robotics Co., Ltd
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

#ifndef HOBOT_SIGI_VIRT_H
#define HOBOT_SIGI_VIRT_H

#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/intc/arm_gicv3.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "qom/object.h"
#include "hw/char/serial.h"
#include "hw/sd/cadence_sdhci.h"
#include "qemu/units.h"
#include "exec/hwaddr.h"

#define TYPE_SIGI_VIRT "sigi-virt"
OBJECT_DECLARE_SIMPLE_TYPE(SigiVirt, SIGI_VIRT)

#define SIGI_VIRT_NR_ACPUS      4
#define SIGI_VIRT_NR_RCPUS      4
#define SIGI_VIRT_NR_UARTS      4
#define SIGI_VIRT_NR_SDHCI      2
#define SIGI_VIRT_NUM_IRQS      256

/* Cadence SDHCI capabilities register */
#define SDHCI_CAPABILITIES  0x70156ac800UL

#define ARCH_GIC_MAINT_IRQ  9

#define ARCH_TIMER_VIRT_IRQ   11
#define ARCH_TIMER_S_EL1_IRQ  13
#define ARCH_TIMER_NS_EL1_IRQ 14
#define ARCH_TIMER_NS_EL2_IRQ 10

#define VIRTUAL_PMU_IRQ 7

enum {
    VIRT_MEM,
    VIRT_GIC_DIST,
    VIRT_GIC_ITS,
    VIRT_GIC_REDIST,
    VIRT_UART,
    VIRT_SDHCI,
    VIRT_LOWMEMMAP_LAST,
};

static const MemMapEntry base_memmap[] = {
    /* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
    [VIRT_GIC_DIST] =           { 0x30B00000, 0x00010000 },
    /* The space in between here is reserved for GICv3 CPU/vCPU/HYP */
    [VIRT_GIC_ITS] =            { 0x30290000, 0x00010000 },
    /* This redistributor space allows up to 2 * 64kB * 4 CPUs */
    [VIRT_GIC_REDIST] =         { 0x30B60000, 0x00080000 },
    [VIRT_UART] =               { 0x39050000, 0x00010000 },
    /* ...repeating for a total of SIGI_VIRT_NR_UARTS, each of that size */
    [VIRT_SDHCI] =              { 0x39030000, 0x00010000 },
    [VIRT_MEM] =                { 0x3000000000UL, 16UL * GiB },
};

static const int a78irqmap[] = {
    [VIRT_UART] = 73,   /* ...to 73 + SIGI_VIRT_NR_UARTS - 1 */
    [VIRT_SDHCI] = 120, /* ... 122 for SDHCI1 */
};

struct SigiVirt {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    struct {
        struct {
            SerialMM uarts[SIGI_VIRT_NR_UARTS];
            CadenceSDHCIState mmc[SIGI_VIRT_NR_SDHCI];
        } peri;

        ARMCPU cpus[SIGI_VIRT_NR_ACPUS];
        GICv3State gic;
        GICv3ITSState its;
    } apu;

    MemoryRegion mr_ddr;

    struct {
        MemoryRegion *mr_ddr;
    } cfg;
};

#endif
