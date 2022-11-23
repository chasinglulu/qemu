/*
 * Model of the Sigi Versal
 *
 * Copyright (C) 2022 Hobot Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef XLNX_VERSAL_H
#define XLNX_VERSAL_H

#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/cpu/cluster.h"
#include "hw/or-irq.h"
#include "hw/sd/sdhci.h"
#include "hw/intc/arm_gicv3.h"
#include "hw/char/pl011.h"
#include "hw/dma/xlnx-zdma.h"
#include "hw/net/cadence_gem.h"
#include "hw/rtc/xlnx-zynqmp-rtc.h"
#include "qom/object.h"
#include "hw/usb/xlnx-usb-subsystem.h"
#include "hw/misc/xlnx-versal-xramc.h"
#include "hw/nvram/xlnx-bbram.h"
#include "hw/nvram/xlnx-versal-efuse.h"
#include "hw/ssi/xlnx-versal-ospi.h"
#include "hw/dma/xlnx_csu_dma.h"
#include "hw/misc/xlnx-versal-crl.h"
#include "hw/misc/xlnx-versal-pmc-iou-slcr.h"
#include "hw/char/serial.h"

#define TYPE_SIGI_VERSAL "sigi-versal"
OBJECT_DECLARE_SIMPLE_TYPE(SigiVersal, SIGI_VERSAL)

#define SIGI_VERSAL_NR_ACPUS   4
#define SIGI_VERSAL_NR_RCPUS   4
#define SIGI_VERSAL_NR_UARTS   2
#define SIGI_VERSAL_NR_IRQS    192

struct SigiVersal {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    struct {
        struct {
            SerialMM uarts[SIGI_VERSAL_NR_UARTS];
        } peri;
        struct {
            MemoryRegion mr;
            CPUClusterState cluster;
            ARMCPU cpu[SIGI_VERSAL_NR_ACPUS];
            GICv3State gic;
        } apu;
    } cpu_subsys;

    MemoryRegion mr_ps;

    struct {
        /* 4 ranges to access DDR.  */
        MemoryRegion mr_ddr_ranges[1];
    } noc;

    struct {
        /* Real-time Processing Unit.  */
        struct {
            MemoryRegion mr;

            CPUClusterState cluster;
            ARMCPU cpu[SIGI_VERSAL_NR_RCPUS];
        } rpu;
    } mcu_subsys;

    struct {
        MemoryRegion *mr_ddr;
    } cfg;
};

/* Memory-map and IRQ definitions. Copied a subset from
 * auto-generated files.  */

#define VERSAL_GIC_MAINT_IRQ        9
#define VERSAL_TIMER_VIRT_IRQ       11
#define VERSAL_TIMER_S_EL1_IRQ      13
#define VERSAL_TIMER_NS_EL1_IRQ     14
#define VERSAL_TIMER_NS_EL2_IRQ     10

#define VERSAL_UART0_IRQ_0         18
#define VERSAL_UART1_IRQ_0         19

#define MM_TOP_RSVD                 0xa0000000U
#define MM_TOP_RSVD_SIZE            0x4000000
#define MM_GIC_APU_DIST_MAIN        0x58000000U
#define MM_GIC_APU_DIST_MAIN_SIZE   0x10000
#define MM_GIC_APU_REDIST_0         0x58040000U
#define MM_GIC_APU_REDIST_0_SIZE    0x80000

#define MM_UART0                    0x43b80000U
#define MM_UART0_SIZE               0x10000
#define MM_UART1                    0x43b90000U
#define MM_UART1_SIZE               0x10000

#define MM_TOP_DDR                0x80000000U
#define MM_TOP_DDR_SIZE           0x400000000ULL

#endif
