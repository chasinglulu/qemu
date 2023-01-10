/*
 * Model of the Hobot Sigi SoC
 *
 * Copyright (C) 2022 Hobot Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef HOBOT_AYERS_SOC_H
#define HOBOT_AYERS_SOC_H

#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/cpu/cluster.h"
#include "hw/intc/arm_gicv3.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "qom/object.h"
#include "hw/char/serial.h"
#include "hw/sd/cadence_sdhci.h"
#include "hw/pci-host/designware.h"
#include "hw/pci-host/gpex.h"
#include "hw/net/cadence_gem.h"
#include "hw/usb/hcd-dwc3.h"

#define TYPE_AYERS_SOC "ayers-soc"
OBJECT_DECLARE_SIMPLE_TYPE(AyersSoC, AYERS_SOC)

#define AYERS_SOC_NR_ACPUS    4
#define AYERS_SOC_NR_RCPUS    4
#define AYERS_SOC_NR_UARTS    2
#define AYERS_SOC_NR_SDHCI    2
#define AYERS_SOC_NR_GEMS     2
#define AYERS_SOC_NR_IRQS     192

struct AyersSoC {
    /*< private >*/
    SysBusDevice parent_obj;

    bool secure;
    bool virt;

    /*< public >*/
    struct {
        struct {
            SerialMM uarts[AYERS_SOC_NR_UARTS];
            CadenceSDHCIState mmc[AYERS_SOC_NR_SDHCI];
            DesignwarePCIEHost dw_pcie;
            GPEXHost pcie;
            CadenceGEMState gem[AYERS_SOC_NR_GEMS];
            USBDWC3 usb;
        } peri;
        struct {
            CPUClusterState cluster;
            ARMCPU cpu[AYERS_SOC_NR_ACPUS];
            GICv3State gic;
            GICv3ITSState its;
        } apu;
    } cpu_subsys;

    struct {
        /* 4 ranges to access DDR.  */
        MemoryRegion mr_ddr_ranges[1];
    } noc;

    struct {
        /* Real-time Processing Unit.  */
        struct {
            CPUClusterState cluster;
            ARMCPU cpu[AYERS_SOC_NR_RCPUS];
        } rpu;
    } mcu_subsys;

    struct {
        MemoryRegion *mr_ddr;
        bool has_emmc;
    } cfg;
};

/* Memory-map and IRQ definitions. Copied a subset from
 * auto-generated files.  */

#define AYERS_SOC_GIC_MAINT_IRQ        9
#define AYERS_SOC_TIMER_VIRT_IRQ       11
#define AYERS_SOC_TIMER_S_EL1_IRQ      13
#define AYERS_SOC_TIMER_NS_EL1_IRQ     14
#define AYERS_SOC_TIMER_NS_EL2_IRQ     10

#define AYERS_SOC_ETH0_IRQ_0           40
#define AYERS_SOC_UART0_IRQ_0          73
#define AYERS_SOC_UART1_IRQ_0          74
#define AYERS_SOC_SDHCI0_IRQ_0         120
#define AYERS_SOC_ETH1_IRQ_0           124
#define AYERS_SOC_PCIE_IRQ_A           127
#define AYERS_SOC_PCIE_IRQ_B           128
#define AYERS_SOC_PCIE_IRQ_C           129
#define AYERS_SOC_PCIE_IRQ_D           130

#define MM_GIC_ITS                  0x50000000U
#define MM_GIC_ITS_SIZE             0x20000
#define MM_GIC_APU_DIST_MAIN        0x58000000U
#define MM_GIC_APU_DIST_MAIN_SIZE   0x10000
#define MM_GIC_APU_REDIST_0         0x58040000U
#define MM_GIC_APU_REDIST_0_SIZE    0x80000

#define MM_UART0                    0x43b80000U
#define MM_UART0_SIZE               0x10000
#define MM_UART1                    0x43b90000U
#define MM_UART1_SIZE               0x10000

#define MM_PERI_SDHCI0              0x48030000U
#define MM_PERI_SDHCI0_SIZE         0x10000

#define MM_PERI_ETH0                0x59110000U
#define MM_PERI_ETH0_SIZE           0x10000
#define MM_PERI_ETH1                0x59120000U
#define MM_PERI_ETH1_SIZE           0x10000

#define MM_PERI_DW_PCIE             0x48070000U
#define MM_PERI_DW_PCIE_SIZE        0x1000
#define MM_PERI_DW_PCIE_PHY         0x48071000U
#define MM_PERI_DW_PCIE_PHY_SIZE    0x1000
#define MM_PERI_DW_PCIE_CFG         0x59C00000U
#define MM_PERI_DW_PCIE_CFG_SIZE    0x400000

/* ECAM-based PCIe host controller*/
#define MM_PERI_PCIE_CFG            0x59C00000U
#define MM_PERI_PCIE_CFG_SIZE       0x400000
#define MM_PERI_PCIE_MMIO           0x60000000U
#define MM_PERI_PCIE_MMIO_SIZE      0x10000000U
#define MM_PERI_PCIE_MMIO_HIGH      0x480000000ULL
#define MM_PERI_PCIE_MMIO_HIGH_SIZE 0x100000000ULL

#define MM_TOP_DDR		    0x80000000U
#define MM_TOP_DDR_SIZE		0x400000000ULL

#endif
