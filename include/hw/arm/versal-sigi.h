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

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/units.h"
#include "hw/arm/boot.h"
#include "hw/intc/arm_gicv3.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "qom/object.h"
#include "hw/char/serial.h"
#include "hw/sd/cadence_sdhci.h"
#include "qemu/log.h"
#include "exec/hwaddr.h"
#include "target/arm/cpu.h"
#include "hw/gpio/dwapb_gpio.h"
#include "hw/pci-host/gpex.h"
#include "hw/net/cadence_gem.h"

#define TYPE_SIGI_VIRT "sigi-virt"
OBJECT_DECLARE_SIMPLE_TYPE(SigiVirt, SIGI_VIRT)

#define SIGI_VIRT_CLUSTER_SIZE  4
#define SIGI_VIRT_NR_ACPUS      4
#define SIGI_VIRT_NR_RCPUS      4
#define SIGI_VIRT_NR_UARTS      4
#define SIGI_VIRT_NR_SDHCI      2
#define SIGI_VIRT_NR_GPIO       2
#define SIGI_VIRT_NR_GEMS       2
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
    VIRT_GPIO,
    VIRT_GEM,
    VIRT_PCIE_ECAM,
    VIRT_PCIE_PIO,
    VIRT_PCIE_MMIO,
    VIRT_PCIE_MMIO_HIGH,
    VIRT_LOWMEMMAP_LAST,
};

static const MemMapEntry base_memmap[] = {
    [VIRT_GIC_ITS] =            { 0x30290000, 0x00010000 },
    /* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
    [VIRT_GIC_DIST] =           { 0x30B00000, 0x00010000 },
    /* This redistributor space allows up to 2 * 64kB * 14 CPUs */
    [VIRT_GIC_REDIST] =         { 0x30B60000, 0x001C0000 },
    [VIRT_GEM] =                { 0x33380000, 0x00010000 },
    [VIRT_PCIE_ECAM] =          { 0x34000000, 0x00400000 },
    [VIRT_PCIE_MMIO] =          { 0x80000000, 0x40000000 },
    [VIRT_PCIE_MMIO_HIGH]=      { 0x8000000000, 0x8000000000 },
    [VIRT_SDHCI] =              { 0x39030000, 0x00010000 },
    [VIRT_UART] =               { 0x39050000, 0x00010000 },
    /* ...repeating for a total of SIGI_VIRT_NR_UARTS, each of that size */
    [VIRT_GPIO] =               { 0x3A120000, 0x00010000 },
    [VIRT_MEM] =                { 0x3000000000UL, 16UL * GiB },
};

static const int a78irqmap[] = {
    [VIRT_UART] = 73,   /* ...to 73 + SIGI_VIRT_NR_UARTS - 1 */
    [VIRT_SDHCI] = 120, /* ... 122 for SDHCI1 */
    [VIRT_GPIO] = 78, /* ...to 78 + SIGI_VIRT_NR_GPIO - 1*/
    [VIRT_PCIE_ECAM] = 127, /* ... to 130 */
    [VIRT_GEM] = 40, /* ... to 41 */
};

struct SigiVirt {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    struct {
        struct {
            SerialMM uarts[SIGI_VIRT_NR_UARTS];
            CadenceSDHCIState mmc[SIGI_VIRT_NR_SDHCI];
            DWAPBGPIOState gpio[SIGI_VIRT_NR_GPIO];
            CadenceGEMState gem[SIGI_VIRT_NR_GEMS];
            GPEXHost pcie;
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

static inline uint64_t virt_cpu_mp_affinity(int idx)
{
    uint64_t mp_aff = arm_cpu_mp_affinity(idx, SIGI_VIRT_CLUSTER_SIZE);

    mp_aff <<= 8;
    return mp_aff;
}

#endif
