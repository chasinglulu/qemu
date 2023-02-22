/*
 *
 * Copyright (c) 2015 Linaro Limited
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
 * Emulate a virtual board which works by passing Linux all the information
 * it needs about what devices are present via the device tree.
 * There are some restrictions about what we can do here:
 *  + we can only present devices whose Linux drivers will work based
 *    purely on the device tree with no platform data at all
 *  + we want to present a very stripped-down minimalist platform,
 *    both because this reduces the security attack surface from the guest
 *    and also because it reduces our exposure to being broken when
 *    the kernel updates its device tree bindings and requires further
 *    information in a device binding that we aren't providing.
 * This is essentially the same approach kvmtool uses.
 */

#ifndef QEMU_ARM_VIRT_H
#define QEMU_ARM_VIRT_H

#include "exec/hwaddr.h"
#include "qemu/notify.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/block/flash.h"
#include "sysemu/kvm.h"
#include "hw/intc/arm_gicv3_common.h"
#include "qom/object.h"

#define NUM_GICV2M_SPIS       64
#define NUM_VIRTIO_TRANSPORTS 32
#define NUM_SMMU_IRQS          4

#define ARCH_GIC_MAINT_IRQ  9

#define ARCH_TIMER_VIRT_IRQ   11
#define ARCH_TIMER_S_EL1_IRQ  13
#define ARCH_TIMER_NS_EL1_IRQ 14
#define ARCH_TIMER_NS_EL2_IRQ 10

#define VIRTUAL_PMU_IRQ 7

#define PPI(irq) ((irq) + 16)

/* See Linux kernel arch/arm64/include/asm/pvclock-abi.h */
#define PVTIME_SIZE_PER_CPU 64

enum {
    VIRT_FLASH,
    VIRT_MEM,
    VIRT_CPUPERIPHS,
    VIRT_GIC_DIST,
    VIRT_GIC_ITS,
    VIRT_GIC_REDIST,
    VIRT_UART,
    VIRT_MMIO,
    VIRT_SDHCI,
    VIRT_FW_CFG,
    VIRT_PCIE,
    VIRT_PCIE_MMIO,
    VIRT_PCIE_PIO,
    VIRT_PCIE_ECAM,
    VIRT_PLATFORM_BUS,
    VIRT_GPIO,
    VIRT_SECURE_UART,
    VIRT_SECURE_MEM,
    VIRT_SECURE_GPIO,
    VIRT_PVTIME,
    VIRT_LOWMEMMAP_LAST,
};

/* indices of IO regions located after the RAM */
enum {
    VIRT_HIGH_GIC_REDIST2 =  VIRT_LOWMEMMAP_LAST,
    VIRT_HIGH_PCIE_ECAM,
    VIRT_HIGH_PCIE_MMIO,
};

typedef enum HobotVirtMSIControllerType {
    VIRT_MSI_CTRL_NONE,
    VIRT_MSI_CTRL_GICV2M,
    VIRT_MSI_CTRL_ITS,
} HobotVirtMSIControllerType;

struct HobotVirtMachineClass {
    MachineClass parent;
    bool disallow_affinity_adjustment;
    bool no_pmu;
    bool claim_edge_triggered_timers;
    bool no_highmem_ecam;
    bool kvm_no_adjvtime;
    bool no_kvm_steal_time;
    bool no_secure_gpio;
    /* Machines < 6.2 have no support for describing cpu topology to guest */
    bool no_cpu_topology;
    bool no_tcg_lpa2;
};

struct HobotVirtMachineState {
    MachineState parent;
    Notifier machine_done;
    FWCfgState *fw_cfg;
    PFlashCFI01 *flash[2];
    bool secure;
    bool highmem;
    bool highmem_ecam;
    bool highmem_mmio;
    bool highmem_redists;
    bool virt;
    bool dtb_randomness;
    HobotVirtMSIControllerType msi_controller;
    struct arm_boot_info bootinfo;
    MemMapEntry *memmap;
    char *pciehb_nodename;
    const int *irqmap;
    int fdt_size;
    uint32_t clock_phandle;
    uint32_t gic_phandle;
    uint32_t msi_phandle;
    int psci_conduit;
    hwaddr highest_gpa;
    DeviceState *gic;
    Notifier powerdown_notifier;
    PCIBus *bus;
};

#define VIRT_ECAM_ID(high) (high ? VIRT_HIGH_PCIE_ECAM : VIRT_PCIE_ECAM)

#define TYPE_VIRT_MACHINE   MACHINE_TYPE_NAME("hobot-virt")
OBJECT_DECLARE_TYPE(HobotVirtMachineState, HobotVirtMachineClass, VIRT_MACHINE)

/* Return number of redistributors that fit in the specified region */
static uint32_t virt_redist_capacity(HobotVirtMachineState *vms, int region)
{
    uint32_t redist_size;

    redist_size = GICV3_REDIST_SIZE;

    return vms->memmap[region].size / redist_size;
}

/* Return the number of used redistributor regions  */
static inline int virt_gicv3_redist_region_count(HobotVirtMachineState *vms)
{
    uint32_t redist0_capacity = virt_redist_capacity(vms, VIRT_GIC_REDIST);

    return (MACHINE(vms)->smp.cpus > redist0_capacity &&
            vms->highmem_redists) ? 2 : 1;
}

#endif /* QEMU_ARM_VIRT_H */