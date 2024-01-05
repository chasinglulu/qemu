/*
 * System-on-Chip PMU register definition
 *
 * Copyright 2023 xinlu.wang
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef SIGI_PMU_H
#define SIGI_PMU_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include <stdint.h>
#include <linux/types.h>

/* PMU register offsets for CPU*/
enum {
    CPU_TOP_0               = 0x01f0,
    CPU_TOP_1               = 0x01f4,
    CPU_TOP_2               = 0x01f8,
    CPU_TOP_3               = 0x01fc,
    CPU_CLUSTER0_0          = 0x0208,
    CPU_CLUSTER0_1          = 0x020c,
    CPU_CLUSTER0_2          = 0x0210,
    CPU_CL0_C0_0            = 0x0214,
    CPU_CL0_C0_1            = 0x0218,
    CPU_CL0_C1_0            = 0x0220,
    CPU_CL0_C1_1            = 0x0224,
    CPU_CL0_C2_0            = 0x022c,
    CPU_CL0_C2_1            = 0x0230,
    CPU_CL0_C3_0            = 0x0238,
    CPU_CL0_C3_1            = 0x023c,
    CPU_CL1_C0_0            = 0x0240,
    CPU_CL1_C0_1            = 0x0244,
    CPU_CL1_C1_0            = 0x0248,
    CPU_CL1_C1_1            = 0x024c,
    CPU_CL1_C2_0            = 0x0250,
    CPU_CL1_C2_1            = 0x0254,
    CPU_CL1_C3_0            = 0x0258,
    CPU_CL1_C3_1            = 0x025c,
};

/* PMU register fields for CPU core Y of cluster X */
enum {
    CPU_CLX_CY_PWR_TRI      = BIT(12),
    CPU_CLX_CY_PWR_OFF      = BIT(1),
    CPU_CLX_CY_PWR_ON       = BIT(0),
};

#define TYPE_SIGI_PMU "sigi.pmu"
typedef struct SIGIPMUState SIGIPMUState;
DECLARE_INSTANCE_CHECKER(SIGIPMUState, SIGI_PMU,
                         TYPE_SIGI_PMU)

#define SIGI_PMU_MM_SIZE    0x00300000U

struct SIGIPMUState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion *mr_shared_ocm;

    uint32_t cpu_top0;
    uint32_t cpu_top1;
    uint32_t cpu_top2;
    uint32_t cpu_top3;
    uint32_t cpu_cluster00;
    uint32_t cpu_cluster01;
    uint32_t cpu_cluster02;
    uint32_t cpu_cl0_c00;
    uint32_t cpu_cl0_c01;
    uint32_t cpu_cl0_c10;
    uint32_t cpu_cl0_c11;
    uint32_t cpu_cl0_c20;
    uint32_t cpu_cl0_c21;
    uint32_t cpu_cl0_c30;
    uint32_t cpu_cl0_c31;
    uint32_t cpu_cl1_c00;
    uint32_t cpu_cl1_c01;
    uint32_t cpu_cl1_c10;
    uint32_t cpu_cl1_c11;
    uint32_t cpu_cl1_c20;
    uint32_t cpu_cl1_c21;
    uint32_t cpu_cl1_c30;
    uint32_t cpu_cl1_c31;
};

#endif