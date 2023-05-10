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

#ifndef HOBOT_SIGIE_VIRT_H
#define HOBOT_SIGIE_VIRT_H

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
#include "hw/register.h"
#include "hw/usb/hcd-dwc3.h"
#include "hw/misc/hobot-sigi-pmu.h"
#include "hw/block/flash.h"

#define TYPE_SIGIE_VIRT "sigie-virt"
OBJECT_DECLARE_SIMPLE_TYPE(SigiEVirt, SIGIE_VIRT)

#define SIGIE_VIRT_CLUSTER_SIZE  4
#define SIGIE_VIRT_CLUSTERS      2
#define SIGIE_VIRT_NR_ACPUS      6
#define SIGIE_VIRT_NR_RCPUS      4
#define SIGIE_VIRT_NR_UARTS      4
#define SIGIE_VIRT_NR_SDHCI      1
#define SIGIE_VIRT_NR_GPIO       2
#define SIGIE_VIRT_NUM_IRQS      960

/* Cadence SDHCI capabilities register */
#define SDHCI_CAPABILITIES  0x70156ac800UL

#define ARCH_GIC_MAINT_IRQ  9

#define ARCH_TIMER_VIRT_IRQ   11
#define ARCH_TIMER_S_EL1_IRQ  13
#define ARCH_TIMER_NS_EL1_IRQ 14
#define ARCH_TIMER_NS_EL2_IRQ 10

#define VIRTUAL_PMU_IRQ     7
#define DDR_SIZE            (48ULL * GiB)

enum {
    VIRT_LOW_MEM,
    VIRT_HIGH_MEM,
    VIRT_FLASH,
    VIRT_GIC_DIST,
    VIRT_GIC_REDIST,
    VIRT_UART,
    VIRT_SDHCI,
    VIRT_GPIO,
    VIRT_DWC_USB,
    VIRT_USB_CTRL,
    VIRT_PCIE_ECAM,
    VIRT_PCIE_PIO,
    VIRT_PCIE_MMIO,
    VIRT_PCIE_MMIO_HIGH,
    VIRT_CPU_PMU,
};

static const MemMapEntry base_memmap[] = {
    [VIRT_FLASH] =              { 0x18000000, 0x08000000 },
    /* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
    [VIRT_GIC_DIST] =           { 0x30800000, 0x00010000 },
    /* This redistributor space allows up to 2 * 64kB * 6 CPUs */
    [VIRT_GIC_REDIST] =         { 0x30860000, 0x000C0000 },
    [VIRT_CPU_PMU] =            { 0x30F00000, 0x00010000 },
    [VIRT_PCIE_ECAM] =          { 0x34000000, 0x00800000 },
    [VIRT_PCIE_MMIO] =          { 0x40000000, 0x40000000 },
    [VIRT_PCIE_MMIO_HIGH]=      { 0x8000000000, 0x8000000000 },
    [VIRT_SDHCI] =              { 0x3A0E0000, 0x00010000 },
    [VIRT_UART] =               { 0x3A0A0000, 0x00010000 },
    /* ...repeating for a total of SIGIE_VIRT_NR_UARTS, each of that size */
    [VIRT_GPIO] =               { 0x3A0F0000, 0x00010000 },
    [VIRT_USB_CTRL] =           { 0x3A000000, 0x00010000 },
    [VIRT_DWC_USB] =            { 0x3A8A0000, 0x00010000 },
    [VIRT_LOW_MEM] =            { 0x80000000, 0x80000000 },
    [VIRT_HIGH_MEM] =           { 0x400000000UL, DDR_SIZE },
};

static const int a78irqmap[] = {
    [VIRT_UART] = 73,   /* ...to 73 + SIGIE_VIRT_NR_UARTS - 1 */
    [VIRT_SDHCI] = 120, /* ... 122 for SDHCI1 */
    [VIRT_GPIO] = 78, /* ...to 78 + SIGIE_VIRT_NR_GPIO - 1*/
    [VIRT_PCIE_ECAM] = 127, /* ... to 130 */
    [VIRT_DWC_USB] = 132,
};

struct SigiEVirt {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    struct {
        struct {
            SerialMM uarts[SIGIE_VIRT_NR_UARTS];
            CadenceSDHCIState mmc[SIGIE_VIRT_NR_SDHCI];
            DWAPBGPIOState gpio[SIGIE_VIRT_NR_GPIO];
            GPEXHost pcie;
            USBDWC3 usb;
        } peri;

        ARMCPU cpus[SIGIE_VIRT_NR_ACPUS];
        GICv3State gic;
    } apu;

    PFlashCFI01 *flash[2];

    MemoryRegion mr_low_ddr;
    MemoryRegion mr_high_ddr;

    struct {
        MemoryRegion *mr_ddr;
        bool has_emmc;
        bool virt;
        bool secure;
    } cfg;
};

static inline uint64_t virt_cpu_mp_affinity(int idx)
{
    uint64_t mp_aff = arm_cpu_mp_affinity(idx, SIGIE_VIRT_CLUSTER_SIZE);

    mp_aff <<= 8;
    return mp_aff;
}

#endif
