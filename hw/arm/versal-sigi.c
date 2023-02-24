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
#include "hw/arm/versal-sigi.h"
#include "qemu/log.h"
#include "hw/misc/unimp.h"
#include "hw/nvme/nvme.h"
#include "hw/gpio/dwapb_gpio.h"

#define SIGI_VIRT_ACPU_TYPE ARM_CPU_TYPE_NAME("cortex-a78ae")

static void create_gpio(SigiVirt *s, int gpio)
{
    MemoryRegion *sysmem = get_system_memory();
    int irq = a78irqmap[gpio];
    hwaddr base = base_memmap[gpio].base;
    hwaddr size = base_memmap[gpio].size;
    DeviceState *gicdev = DEVICE(&s->apu.gic);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->apu.peri.gpio); i++) {
        DeviceState *dev;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), "gpio[*]", &s->apu.peri.gpio[i],
                                TYPE_DWAPB_GPIO);
        dev = DEVICE(&s->apu.peri.gpio[i]);
        dev->id = g_strdup_printf("gpio%d", i);
        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(sysmem, base, mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(gicdev, irq));

        base += size;
        irq += 1;
    }
}

static void create_uart(SigiVirt *s, int uart)
{
    MemoryRegion *sysmem = get_system_memory();
    int irq = a78irqmap[uart];
    hwaddr base = base_memmap[uart].base;
    hwaddr size = base_memmap[uart].size;
    DeviceState *gicdev = DEVICE(&s->apu.gic);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->apu.peri.uarts); i++) {
        char *name = g_strdup_printf("uart%d", i);
        DeviceState *dev;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), name, &s->apu.peri.uarts[i],
                                TYPE_SERIAL_MM);
        dev = DEVICE(&s->apu.peri.uarts[i]);
        qdev_prop_set_uint8(dev, "regshift", 2);
        qdev_prop_set_uint32(dev, "baudbase", 115200);
        qdev_prop_set_uint8(dev, "endianness", DEVICE_LITTLE_ENDIAN);
        qdev_prop_set_chr(dev, "chardev", serial_hd(i));
        sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(sysmem, base, mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(gicdev, irq));

        base += size;
        irq += 1;
        g_free(name);
    }
}

static void create_sdhci(SigiVirt *s, int sdhci)
{
    MemoryRegion *sysmem = get_system_memory();
    int irq = a78irqmap[sdhci];
    hwaddr base = base_memmap[sdhci].base;
    hwaddr size = base_memmap[sdhci].size;
    DeviceState *gicdev = DEVICE(&s->apu.gic);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->apu.peri.mmc); i++) {
        DeviceState *dev;
        MemoryRegion *mr;
        irq += i * 2;

        object_initialize_child(OBJECT(s), "sdhci[*]", &s->apu.peri.mmc[i],
                                TYPE_CADENCE_SDHCI);
        dev = DEVICE(&s->apu.peri.mmc[i]);
        dev->id = g_strdup_printf("sdhci%d", i);
        object_property_set_uint(OBJECT(dev), "index", i,
                                 &error_fatal);
        object_property_set_uint(OBJECT(dev), "capareg", SDHCI_CAPABILITIES,
                                 &error_fatal);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(sysmem,
                                    base + i * size, mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(gicdev, irq));
    }
}

static void create_gic(SigiVirt *s)
{
    MemoryRegion *sysmem = get_system_memory();
    int nr_apu = ARRAY_SIZE(s->apu.cpus);
    const char *gictype = gicv3_class_name();
    /* We create a standalone GIC */
    SysBusDevice *gicbusdev;
    DeviceState *gicdev;
    int i;

    object_initialize_child(OBJECT(s), "apu-gic", &s->apu.gic, gictype);
    gicdev = DEVICE(&s->apu.gic);
    qdev_prop_set_uint32(gicdev, "revision", 3);
    qdev_prop_set_uint32(gicdev, "num-cpu", nr_apu);
    /* Note that the num-irq property counts both internal and external
     * interrupts; there are always 32 of the former (mandated by GIC spec).
     */
    qdev_prop_set_uint32(gicdev, "num-irq",
                            SIGI_VIRT_NUM_IRQS + 32);
    qdev_prop_set_uint32(gicdev, "len-redist-region-count", 1);
    qdev_prop_set_uint32(gicdev, "redist-region-count[0]", nr_apu);
    qdev_prop_set_bit(gicdev, "has-lpi", true);
    object_property_set_link(OBJECT(gicdev), "sysmem",
                            OBJECT(sysmem), &error_fatal);

    gicbusdev = SYS_BUS_DEVICE(gicdev);
    sysbus_realize(gicbusdev, &error_fatal);
    sysbus_mmio_map(gicbusdev, 0, base_memmap[VIRT_GIC_DIST].base);
    sysbus_mmio_map(gicbusdev, 1, base_memmap[VIRT_GIC_REDIST].base);

    /* Wire the outputs from each CPU's generic timer and the GICv3
     * maintenance interrupt signal to the appropriate GIC PPI inputs,
     * and the GIC's IRQ/FIQ/VIRQ/VFIQ interrupt outputs to the CPU's inputs.
     */
    for (i = 0; i < nr_apu; i++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
        int ppibase = SIGI_VIRT_NUM_IRQS + i * GIC_INTERNAL + GIC_NR_SGIS;
        int irq;
        /* Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs we use for the virt board.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
        };

        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(gicdev,
                                                   ppibase + timer_irq[irq]));
        }

        qemu_irq irq_in = qdev_get_gpio_in(gicdev,
                                            ppibase + ARCH_GIC_MAINT_IRQ);
        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                                        0, irq_in);

        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                                    qdev_get_gpio_in(gicdev, ppibase
                                                     + VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + nr_apu,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * nr_apu,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * nr_apu,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }
}

static void create_apu(SigiVirt *s)
{
    MemoryRegion *sysmem = get_system_memory();
    int i;

    for (i = 0; i < ARRAY_SIZE(s->apu.cpus); i++) {
        Object *cpuobj;

        object_initialize_child(OBJECT(s), "apu[*]", &s->apu.cpus[i],
                                SIGI_VIRT_ACPU_TYPE);
        cpuobj = OBJECT(&s->apu.cpus[i]);
        if (i) {
            /* Secondary CPUs start in powered-down state */
            object_property_set_bool(cpuobj, "start-powered-off", true,
                                        &error_abort);
        }

        object_property_set_int(cpuobj, "mp-affinity",
                                virt_cpu_mp_affinity(i), NULL);

        object_property_set_bool(cpuobj, "has_el3", false, NULL);
        object_property_set_bool(cpuobj, "has_el2", false, NULL);
        object_property_set_bool(cpuobj, "pmu", false, NULL);

        object_property_set_link(cpuobj, "memory", OBJECT(sysmem),
                                    &error_abort);

        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
    }
}

/* This takes the board allocated linear DDR memory and creates aliases
 * for each split DDR range/aperture on the address map.
 */
static void create_ddr_memmap(SigiVirt *s, int virt_mem)
{
    uint64_t cfg_ddr_size = memory_region_size(s->cfg.mr_ddr);
    MemoryRegion *sysmem = get_system_memory();
    hwaddr base = base_memmap[virt_mem].base;
    hwaddr size = base_memmap[virt_mem].size;
    uint64_t offset = 0;
    char *name;
    uint64_t mapsize;

    mapsize = cfg_ddr_size < size ? cfg_ddr_size : size;
    name = g_strdup_printf("sigi-ddr");
    /* Create the MR alias.  */
    memory_region_init_alias(&s->mr_ddr, OBJECT(s),
                                name, s->cfg.mr_ddr,
                                offset, mapsize);

    /* Map it onto the main system MR.  */
    memory_region_add_subregion(sysmem, base, &s->mr_ddr);
    g_free(name);
}

static void sigi_virt_realize(DeviceState *dev, Error **errp)
{
    SigiVirt *s = SIGI_VIRT(dev);

    create_apu(s);
    create_gic(s);
    create_uart(s, VIRT_UART);
    create_sdhci(s, VIRT_SDHCI);
    create_gpio(s, VIRT_GPIO);
    create_ddr_memmap(s, VIRT_MEM);
}

static Property sigi_virt_properties[] = {
    DEFINE_PROP_LINK("sigi-virt.ddr", SigiVirt, cfg.mr_ddr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST()
};

static void sigi_virt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sigi_virt_realize;
    device_class_set_props(dc, sigi_virt_properties);
}

static void sigi_virt_init(Object *obj)
{
}

static const TypeInfo sigi_soc_info = {
    .name = TYPE_SIGI_VIRT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SigiVirt),
    .instance_init = sigi_virt_init,
    .class_init = sigi_virt_class_init,
};

static void sigi_soc_register_types(void)
{
    type_register_static(&sigi_soc_info);
}

type_init(sigi_soc_register_types);
