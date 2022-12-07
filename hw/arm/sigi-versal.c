/*
 * Sigi Versal SoC model.
 *
 * Copyright (c) 2022 Hobot Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */


#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "hw/arm/boot.h"
#include "kvm_arm.h"
#include "hw/misc/unimp.h"
#include "hw/arm/sigi-versal.h"
#include "qemu/log.h"
#include "hw/misc/unimp.h"
#include "hw/nvme/nvme.h"


#define SIGI_VERSAL_ACPU_TYPE ARM_CPU_TYPE_NAME("cortex-a78ae")
#define SIGI_VERSAL_RCPU_TYPE ARM_CPU_TYPE_NAME("cortex-r52")

static bool virt_get_secure(Object *obj, Error **errp)
{
    SigiVersal *s = SIGI_VERSAL(obj);

    return s->secure;
}

static void virt_set_secure(Object *obj, bool value, Error **errp)
{
    SigiVersal *s = SIGI_VERSAL(obj);

    s->secure = value;
}

static bool virt_get_virt(Object *obj, Error **errp)
{
    SigiVersal *s = SIGI_VERSAL(obj);

    return s->virt;
}

static void virt_set_virt(Object *obj, bool value, Error **errp)
{
    SigiVersal *s = SIGI_VERSAL(obj);

    s->virt = value;
}

static void versal_create_apu_cpus(SigiVersal *s)
{
    int i;

    object_initialize_child(OBJECT(s), "apu-cluster", &s->cpu_subsys.apu.cluster,
                            TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->cpu_subsys.apu.cluster), "cluster-id", 0);

    for (i = 0; i < ARRAY_SIZE(s->cpu_subsys.apu.cpu); i++) {
        Object *obj;

        object_initialize_child(OBJECT(&s->cpu_subsys.apu.cluster),
                                "apu-cpu[*]", &s->cpu_subsys.apu.cpu[i],
                                SIGI_VERSAL_ACPU_TYPE);
        obj = OBJECT(&s->cpu_subsys.apu.cpu[i]);
        if (i) {
            /* Secondary CPUs start in powered-down state */
            object_property_set_bool(obj, "start-powered-off", true,
                                     &error_abort);
        }

        s->cpu_subsys.apu.cpu[i].mp_affinity = i * 0x100;
        object_property_set_int(obj, "core-count", ARRAY_SIZE(s->cpu_subsys.apu.cpu),
                                &error_abort);
        object_property_set_link(obj, "memory", OBJECT(get_system_memory()),
                                 &error_abort);
        if (!s->secure)
            object_property_set_bool(obj, "has_el3", false, NULL);

        if (!s->virt)
            object_property_set_bool(obj, "has_el2", false, NULL);

        qdev_realize(DEVICE(obj), NULL, &error_fatal);
    }

    qdev_realize(DEVICE(&s->cpu_subsys.apu.cluster), NULL, &error_fatal);
}

static void versal_create_its(SigiVersal *s)
{
    const char *itsclass = its_class_name();
    DeviceState *dev;
    MemoryRegion *mr;

    printf("itsclass: %s\n", itsclass);

    if (strcmp(itsclass, "arm-gicv3-its")) {
            itsclass = NULL;
    }

    if (!itsclass) {
        /* Do nothing if not supported */
        return;
    }

    object_initialize_child(OBJECT(s), "apu-gic-its", &s->cpu_subsys.apu.its,
                            itsclass);
    dev = DEVICE(&s->cpu_subsys.apu.its);
    //object_property_set_link(OBJECT(dev), "parent-gicv3", OBJECT(&s->cpu_subsys.apu.gic), &error_abort);
    object_property_set_link(OBJECT(dev), "parent-gicv3", OBJECT(&s->cpu_subsys.apu.gic), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->cpu_subsys.apu.its), 0);
    memory_region_add_subregion(get_system_memory(), MM_GIC_ITS, mr);
}

static void versal_create_apu_gic(SigiVersal *s, qemu_irq *pic)
{
    static const uint64_t addrs[] = {
        MM_GIC_APU_DIST_MAIN,
        MM_GIC_APU_REDIST_0
    };
    SysBusDevice *gicbusdev;
    DeviceState *gicdev;
    int nr_apu_cpus = ARRAY_SIZE(s->cpu_subsys.apu.cpu);
    int i;

    object_initialize_child(OBJECT(s), "apu-gic", &s->cpu_subsys.apu.gic,
                            gicv3_class_name());
    gicbusdev = SYS_BUS_DEVICE(&s->cpu_subsys.apu.gic);
    gicdev = DEVICE(&s->cpu_subsys.apu.gic);
    qdev_prop_set_uint32(gicdev, "revision", 3);
    qdev_prop_set_uint32(gicdev, "num-cpu", nr_apu_cpus);
    qdev_prop_set_uint32(gicdev, "num-irq", SIGI_VERSAL_NR_IRQS + 32);
    qdev_prop_set_uint32(gicdev, "len-redist-region-count", 1);
    qdev_prop_set_uint32(gicdev, "redist-region-count[0]", nr_apu_cpus);
    object_property_set_link(OBJECT(gicdev), "sysmem", OBJECT(get_system_memory()), &error_fatal);
    qdev_prop_set_bit(gicdev, "has-lpi", true);
    qdev_prop_set_bit(gicdev, "has-security-extensions", true);

    sysbus_realize(SYS_BUS_DEVICE(&s->cpu_subsys.apu.gic), &error_fatal);

    for (i = 0; i < ARRAY_SIZE(addrs); i++) {
        MemoryRegion *mr;

        mr = sysbus_mmio_get_region(gicbusdev, i);
        memory_region_add_subregion(get_system_memory(), addrs[i], mr);
    }

    for (i = 0; i < nr_apu_cpus; i++) {
        DeviceState *cpudev = DEVICE(&s->cpu_subsys.apu.cpu[i]);
        int ppibase = SIGI_VERSAL_NR_IRQS + i * GIC_INTERNAL + GIC_NR_SGIS;
        qemu_irq maint_irq;
        int ti;
        /* Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = VERSAL_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = VERSAL_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = VERSAL_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = VERSAL_TIMER_S_EL1_IRQ,
        };

        for (ti = 0; ti < ARRAY_SIZE(timer_irq); ti++) {
            qdev_connect_gpio_out(cpudev, ti,
                                  qdev_get_gpio_in(gicdev,
                                                   ppibase + timer_irq[ti]));
        }
        maint_irq = qdev_get_gpio_in(gicdev,
                                        ppibase + VERSAL_GIC_MAINT_IRQ);
        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                                    0, maint_irq);
        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + nr_apu_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * nr_apu_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * nr_apu_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    for (i = 0; i < SIGI_VERSAL_NR_IRQS; i++) {
        pic[i] = qdev_get_gpio_in(gicdev, i);
    }
    versal_create_its(s);
}

static void versal_create_rpu_cpus(SigiVersal *s)
{
    int i;

    object_initialize_child(OBJECT(s), "rpu-cluster", &s->mcu_subsys.rpu.cluster,
                            TYPE_CPU_CLUSTER);
    qdev_prop_set_uint32(DEVICE(&s->mcu_subsys.rpu.cluster), "cluster-id", 1);

    for (i = 0; i < ARRAY_SIZE(s->mcu_subsys.rpu.cpu); i++) {
        Object *obj;

        object_initialize_child(OBJECT(&s->mcu_subsys.rpu.cluster),
                                "rpu-cpu[*]", &s->mcu_subsys.rpu.cpu[i],
                                SIGI_VERSAL_RCPU_TYPE);
        obj = OBJECT(&s->mcu_subsys.rpu.cpu[i]);
        object_property_set_bool(obj, "start-powered-off", true,
                                 &error_abort);

        object_property_set_int(obj, "mp-affinity", 0x100 | i, &error_abort);
        object_property_set_int(obj, "core-count", ARRAY_SIZE(s->mcu_subsys.rpu.cpu),
                                &error_abort);
        object_property_set_link(obj, "memory", OBJECT(get_system_memory()),
                                 &error_abort);
        qdev_realize(DEVICE(obj), NULL, &error_fatal);
    }

    qdev_realize(DEVICE(&s->mcu_subsys.rpu.cluster), NULL, &error_fatal);
}

static void versal_create_uarts(SigiVersal *s, qemu_irq *pic)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->cpu_subsys.peri.uarts); i++) {
        static const int irqs[] = { VERSAL_UART1_IRQ_0, VERSAL_UART0_IRQ_0};
        static const uint64_t addrs[] = { MM_UART1, MM_UART0 };
        char *name = g_strdup_printf("uart%d", i);
        DeviceState *dev;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), name, &s->cpu_subsys.peri.uarts[i],
                                TYPE_SERIAL_MM);
        dev = DEVICE(&s->cpu_subsys.peri.uarts[i]);
        qdev_prop_set_uint8(dev, "regshift", 2);
        qdev_prop_set_uint32(dev, "baudbase", 115200);
        qdev_prop_set_uint8(dev, "endianness", DEVICE_LITTLE_ENDIAN);
        qdev_prop_set_chr(dev, "chardev", serial_hd(i));
        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(get_system_memory(), addrs[i], mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[irqs[i]]);
        g_free(name);
    }
}

/*
static void versal_create_dw_pcie(SigiVersal *s, qemu_irq *pic)
{

    static const int irqs[] = { VERSAL_PCIE_IRQ_A, VERSAL_PCIE_IRQ_B,
                                VERSAL_PCIE_IRQ_C, VERSAL_PCIE_IRQ_D};
    DeviceState *dev;
    MemoryRegion *mr;
    int i;

    object_initialize_child(OBJECT(s), "dw_pcie", &s->cpu_subsys.peri.dw_pcie,
                                TYPE_DESIGNWARE_PCIE_HOST);
    dev = DEVICE(&s->cpu_subsys.peri.dw_pcie);
    sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion(&s->mr_ps, MM_PERI_DW_PCIE, mr);

    for (i = 0; i < ARRAY_SIZE(irqs); i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, pic[irqs[i]]);
    }
}
*/

static void versal_create_pcie(SigiVersal *s, qemu_irq *pic)
{

    static const int irqs[] = { VERSAL_PCIE_IRQ_A, VERSAL_PCIE_IRQ_B,
                                VERSAL_PCIE_IRQ_C, VERSAL_PCIE_IRQ_D};
    DeviceState *dev;
    MemoryRegion *mmio_alias;
    MemoryRegion *mmio_reg;
    MemoryRegion *ecam_alias;
    MemoryRegion *ecam_reg;
    int i;

    object_initialize_child(OBJECT(s), "pcie", &s->cpu_subsys.peri.pcie,
                                TYPE_GPEX_HOST);
    dev = DEVICE(&s->cpu_subsys.peri.pcie);
    sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

    /* Map only the first size_ecam bytes of ECAM space */
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, MM_PERI_PCIE_CFG_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                MM_PERI_PCIE_CFG, ecam_alias);

    /* Map the MMIO window into system address space so as to expose
     * the section of PCI MMIO space which starts at the same base address
     * (ie 1:1 mapping for that part of PCI MMIO space visible through
     * the window).
     */
    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, MM_PERI_PCIE_MMIO, MM_PERI_PCIE_MMIO_SIZE);
    memory_region_add_subregion(get_system_memory(), MM_PERI_PCIE_MMIO, mmio_alias);

    /* Map high MMIO space */
    MemoryRegion *high_mmio_alias = g_new0(MemoryRegion, 1);
    memory_region_init_alias(high_mmio_alias, OBJECT(dev), "pcie-mmio-high",
                             mmio_reg, MM_PERI_PCIE_MMIO_HIGH, MM_PERI_PCIE_MMIO_HIGH_SIZE);
    memory_region_add_subregion(get_system_memory(), MM_PERI_PCIE_MMIO_HIGH,
                                high_mmio_alias);

    for (i = 0; i < ARRAY_SIZE(irqs); i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, pic[irqs[i]]);
        gpex_set_irq_num(GPEX_HOST(dev), i, irqs[i]);
    }
}

#define SDHCI_CAPABILITIES 0x70146ec800
static void versal_create_sdhci(SigiVersal *s, qemu_irq *pic)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->cpu_subsys.peri.mmc); i++) {
        DeviceState *dev;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), "sdhci[*]", &s->cpu_subsys.peri.mmc[i],
                                TYPE_CADENCE_SDHCI);
        dev = DEVICE(&s->cpu_subsys.peri.mmc[i]);

        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(get_system_memory(),
                                    MM_PERI_SDHCI0 + i * MM_PERI_SDHCI0_SIZE, mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                           pic[VERSAL_SDHCI0_IRQ_0 + i * 2]);
    }
}


/* This takes the board allocated linear DDR memory and creates aliases
 * for each split DDR range/aperture on the Versal address map.
 */
static void versal_map_ddr(SigiVersal *s)
{
    uint64_t size = memory_region_size(s->cfg.mr_ddr);
    /* Describes the various split DDR access regions.  */
    static const struct {
        uint64_t base;
        uint64_t size;
    } addr_ranges[] = {
        { MM_TOP_DDR, MM_TOP_DDR_SIZE },
    };
    uint64_t offset = 0;
    int i;

    assert(ARRAY_SIZE(addr_ranges) == ARRAY_SIZE(s->noc.mr_ddr_ranges));
    for (i = 0; i < ARRAY_SIZE(addr_ranges) && size; i++) {
        char *name;
        uint64_t mapsize;

        mapsize = size < addr_ranges[i].size ? size : addr_ranges[i].size;
        name = g_strdup_printf("noc-ddr-range%d", i);
        /* Create the MR alias.  */
        memory_region_init_alias(&s->noc.mr_ddr_ranges[i], OBJECT(s),
                                 name, s->cfg.mr_ddr,
                                 offset, mapsize);

        /* Map it onto the NoC MR.  */
        memory_region_add_subregion(get_system_memory(), addr_ranges[i].base,
                                    &s->noc.mr_ddr_ranges[i]);
        offset += mapsize;
        size -= mapsize;
        g_free(name);
    }
}

static void versal_unimp(SigiVersal *s)
{
    //create_unimplemented_device("pcie-phy", MM_PERI_PCIE_PHY, MM_PERI_PCIE_PHY_SIZE);
}

static void sigi_versal_realize(DeviceState *dev, Error **errp)
{
    SigiVersal *s = SIGI_VERSAL(dev);
    qemu_irq pic[SIGI_VERSAL_NR_IRQS];

    versal_create_apu_cpus(s);
    versal_create_apu_gic(s, pic);
    versal_create_rpu_cpus(s);
    versal_create_uarts(s, pic);
    versal_create_sdhci(s, pic);
    //versal_create_dw_pcie(s, pic);
    versal_create_pcie(s, pic);
    versal_map_ddr(s);
    versal_unimp(s);

    //memory_region_add_subregion_overlap(&s->cpu_subsys.apu.mr, 0, &s->mr_ps, 0);
}

static void sigi_versal_init(Object *obj)
{
    //SigiVersal *s = SIGI_VERSAL(obj);

    //memory_region_init(&s->cpu_subsys.apu.mr, obj, "mr-apu", UINT64_MAX);
    //memory_region_init(&s->mcu_subsys.rpu.mr, obj, "mr-rpu", UINT64_MAX);
    //memory_region_init(&s->mr_ps, obj, "mr-ps-switch", UINT64_MAX);
}

static Property sigi_versal_properties[] = {
    DEFINE_PROP_LINK("ddr", SigiVersal, cfg.mr_ddr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST()
};

static void sigi_versal_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sigi_versal_realize;
    device_class_set_props(dc, sigi_versal_properties);
    object_class_property_add_bool(klass, "secure", virt_get_secure,
                                   virt_set_secure);
    object_class_property_set_description(klass, "secure",
                                                "Set on/off to enable/disable the ARM "
                                                "Security Extensions (TrustZone)");

    object_class_property_add_bool(klass, "virtualization", virt_get_virt,
                                   virt_set_virt);
    object_class_property_set_description(klass, "virtualization",
                                          "Set on/off to enable/disable emulating a "
                                          "guest CPU which implements the ARM "
                                          "Virtualization Extensions");
    /* No VMSD since we haven't got any top-level SoC state to save.  */
}

static const TypeInfo sigi_versal_info = {
    .name = TYPE_SIGI_VERSAL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SigiVersal),
    .instance_init = sigi_versal_init,
    .class_init = sigi_versal_class_init,
};

static void sigi_versal_register_types(void)
{
    type_register_static(&sigi_versal_info);
}

type_init(sigi_versal_register_types);
