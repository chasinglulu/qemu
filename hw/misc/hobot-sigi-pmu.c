/*
 * Hobot Sigi System-on-Chip PMU emulation
 *
 * Copyright 2023 xinlu.wang
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/core/cpu.h"
#include "target/arm/arm-powerctl.h"
#include "target/arm/cpu.h"
#include "hw/misc/hobot-sigi-pmu.h"
#include "trace.h"

struct cpu_offset_map {
    unsigned long offset;
    int cpu_idx;
} cpu_map [] = {
    {CPU_CL0_C0_0, 0x000},
    {CPU_CL0_C1_0, 0x100},
    {CPU_CL0_C2_0, 0x200},
    {CPU_CL0_C3_0, 0x300},
    {CPU_CL1_C0_0, 0x10000},
    {CPU_CL1_C1_0, 0x10100},
};

static int get_cpu_idx_by_offset(unsigned long offset)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(cpu_map); i++) {
        if (cpu_map[i].offset == offset)
            return cpu_map[i].cpu_idx;
    }

    return -1;
}

static void sigi_update_state(SIGIPMUState *s, unsigned long offset)
{
    ARMCPU *target_cpu = NULL;
    bool target_aa64;
    int ret;
    int cpu_idx = get_cpu_idx_by_offset(offset);
    bool power_on = false;
    uint64_t entry;
    void *ocm_reg = NULL;

    if (cpu_idx < 0)
        return;

    target_cpu = ARM_CPU(arm_get_cpu_by_id(cpu_idx));
    if (!target_cpu) {
        /*
         * Called with a bogus value for cpu_id. Guest error will
         * already have been logged, we can simply return here.
         */
        return;
    }
    target_aa64 = arm_feature(&target_cpu->env, ARM_FEATURE_AARCH64);

    ocm_reg = memory_region_get_ram_ptr(s->mr_shared_ocm);
    if (!ocm_reg) {
        qemu_log_mask(LOG_GUEST_ERROR, "Can't find shared OCM region.\n");
        return;
    }
    entry = *((uint64_t *)(ocm_reg));

    trace_sigi_update_state(cpu_idx, entry);

    switch (offset) {
    case CPU_CL0_C0_0:
        if (s->cpu_cl0_c00 & CPU_CLX_CY_PWR_TRI) {
            power_on = true;
            s->cpu_cl0_c01 = deposit32(s->cpu_cl0_c01, 0, 2, 1);
        } else {
            power_on = false;
            s->cpu_cl0_c01 = deposit32(s->cpu_cl0_c01, 0, 2, 2);
        }
        break;

    case CPU_CL0_C1_0:
        if (s->cpu_cl0_c10 & CPU_CLX_CY_PWR_TRI) {
            power_on = true;
            s->cpu_cl0_c11 = deposit32(s->cpu_cl0_c11, 0, 2, 1);
        } else {
            power_on = false;
            s->cpu_cl0_c11 = deposit32(s->cpu_cl0_c11, 0, 2, 2);
        }
        break;

    case CPU_CL0_C2_0:
        if (s->cpu_cl0_c20 & CPU_CLX_CY_PWR_TRI) {
            power_on = true;
            s->cpu_cl0_c21 = deposit32(s->cpu_cl0_c21, 0, 2, 1);
        } else {
            power_on = false;
            s->cpu_cl0_c21 = deposit32(s->cpu_cl0_c21, 0, 2, 2);
        }
        break;

    case CPU_CL0_C3_0:
        if (s->cpu_cl0_c30 & CPU_CLX_CY_PWR_TRI) {
            power_on = true;
            s->cpu_cl0_c31 = deposit32(s->cpu_cl0_c31, 0, 2, 1);
        } else {
            power_on = false;
            s->cpu_cl0_c31 = deposit32(s->cpu_cl0_c31, 0, 2, 2);
        }
        break;

    case CPU_CL1_C0_0:
        if (s->cpu_cl1_c00 & CPU_CLX_CY_PWR_TRI) {
            power_on = true;
            s->cpu_cl1_c00 = deposit32(s->cpu_cl1_c00, 0, 2, 1);
        } else {
            power_on = false;
            s->cpu_cl1_c00 = deposit32(s->cpu_cl1_c00, 0, 2, 2);
        }
        break;

    case CPU_CL1_C1_0:
        if (s->cpu_cl1_c10 & CPU_CLX_CY_PWR_TRI) {
            power_on = true;
            s->cpu_cl1_c10 = deposit32(s->cpu_cl1_c10, 0, 2, 1);
        } else {
            power_on = false;
            s->cpu_cl1_c10 = deposit32(s->cpu_cl1_c10, 0, 2, 2);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    if (power_on) {
        ret = arm_set_cpu_on(cpu_idx, entry, 0, 3, target_aa64);
        if (ret != QEMU_ARM_POWERCTL_RET_SUCCESS)
            error_report("%s: failed to bring up CPU %d: err %d",
                         __func__, cpu_idx, ret);
    } else {
        ret = arm_set_cpu_off(cpu_idx);
        if (ret != QEMU_ARM_POWERCTL_RET_SUCCESS)
            error_report("%s: failed to power off CPU %d: err %d",
                         __func__, cpu_idx, ret);
    }
}

static uint64_t sigi_pmu_read(void *opaque, hwaddr offset, unsigned int size)
{
    SIGIPMUState *s = SIGI_PMU(opaque);
    uint64_t r = 0;

    switch (offset) {

    case CPU_CL0_C0_0:
        r = s->cpu_cl0_c00;
        break;

    case CPU_CL0_C0_1:
        r = s->cpu_cl0_c01;
        break;

    case CPU_CL0_C1_0:
        r = s->cpu_cl0_c10;
        break;

    case CPU_CL0_C1_1:
        r = s->cpu_cl0_c11;
        break;

    case CPU_CL0_C2_0:
        r = s->cpu_cl0_c20;
        break;

    case CPU_CL0_C2_1:
        r = s->cpu_cl0_c21;
        break;

    case CPU_CL0_C3_0:
        r = s->cpu_cl0_c30;
        break;

    case CPU_CL0_C3_1:
        r = s->cpu_cl0_c31;
        break;

    case CPU_CL1_C0_0:
        r = s->cpu_cl1_c00;
        break;

    case CPU_CL1_C0_1:
        r = s->cpu_cl1_c01;
        break;

    case CPU_CL1_C1_0:
        r = s->cpu_cl1_c10;
        break;

    case CPU_CL1_C1_1:
        r = s->cpu_cl1_c11;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    trace_sigi_pmu_read(offset, r, size);

    return r;
}

static void sigi_pmu_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned int size)
{
    SIGIPMUState *s = SIGI_PMU(opaque);

    trace_sigi_pmu_write(offset, value, size);

    switch (offset) {

    case CPU_CL0_C0_0:
        s->cpu_cl0_c00 = value;
        break;

    /* only read register */
    case CPU_CL0_C0_1:
        break;

    case CPU_CL0_C1_0:
        s->cpu_cl0_c10 = value;
        break;

    case CPU_CL0_C1_1:
        break;

    case CPU_CL0_C2_0:
        s->cpu_cl0_c20 = value;
        break;

    case CPU_CL0_C2_1:
        break;

    case CPU_CL0_C3_0:
        s->cpu_cl0_c30 = value;
        break;

    case CPU_CL0_C3_1:
        break;

    case CPU_CL1_C0_0:
        s->cpu_cl1_c00 = value;
        break;

    case CPU_CL1_C0_1:
        break;

    case CPU_CL1_C1_0:
        s->cpu_cl1_c10 = value;
        break;

    case CPU_CL1_C1_1:
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    sigi_update_state(s, offset);
}

static const MemoryRegionOps sigi_pmu_ops = {
    .read =  sigi_pmu_read,
    .write = sigi_pmu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void sigi_pmu_reset(DeviceState *dev)
{
    SIGIPMUState *s = SIGI_PMU(dev);

    s->cpu_cl0_c00 = 0x00000860;
    s->cpu_cl0_c01 = 0x04000081;    /* CPU0 ON by default */

    s->cpu_cl0_c10 = 0x00000860;
    s->cpu_cl0_c11 = 0x08000102;
    s->cpu_cl0_c20 = 0x00000860;
    s->cpu_cl0_c21 = 0x08000102;
    s->cpu_cl0_c30 = 0x00000860;
    s->cpu_cl0_c31 = 0x08000102;
    s->cpu_cl1_c00 = 0x00000860;
    s->cpu_cl1_c01 = 0x08000102;
    s->cpu_cl1_c10 = 0x00000860;
    s->cpu_cl1_c11 = 0x08000102;
}

static const VMStateDescription vmstate_sigi_pmu = {
    .name = TYPE_SIGI_PMU,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cpu_cl0_c00,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl0_c01,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl0_c10,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl0_c11,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl0_c20,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl0_c21,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl0_c30,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl0_c31,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl1_c00,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl1_c01,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl1_c10,        SIGIPMUState),
        VMSTATE_UINT32(cpu_cl1_c11,        SIGIPMUState),
        VMSTATE_END_OF_LIST()
    }
};

static Property sigi_pmu_properties[] = {
    DEFINE_PROP_LINK("shared-ocm", SIGIPMUState, mr_shared_ocm, TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void sigi_pmu_realize(DeviceState *dev, Error **errp)
{
    SIGIPMUState *s = SIGI_PMU(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &sigi_pmu_ops, s,
            TYPE_SIGI_PMU, SIGI_PMU_MM_SIZE);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void sigi_pmu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, sigi_pmu_properties);
    dc->vmsd = &vmstate_sigi_pmu;
    dc->realize = sigi_pmu_realize;
    dc->reset = sigi_pmu_reset;
    dc->desc = "TOP PMU";

}

static const TypeInfo sigi_pmu_info = {
    .name = TYPE_SIGI_PMU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SIGIPMUState),
    .class_init = sigi_pmu_class_init
};

static void sigi_pmu_register_types(void)
{
    type_register_static(&sigi_pmu_info);
}

type_init(sigi_pmu_register_types)
