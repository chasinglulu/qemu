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
#include "sysemu/blockdev.h"
#include "sysemu/reset.h"

#define SIGI_VIRT_ACPU_TYPE ARM_CPU_TYPE_NAME("cortex-a78ae")

static bool sigi_virt_get_virt(Object *obj, Error **errp)
{
    SigiVirt *s = SIGI_VIRT(obj);

    return s->cfg.virt;
}

static void sigi_virt_set_virt(Object *obj, bool value, Error **errp)
{
    SigiVirt *s = SIGI_VIRT(obj);

    s->cfg.virt = value;
}

static bool virt_get_secure(Object *obj, Error **errp)
{
    SigiVirt *s = SIGI_VIRT(obj);

    return s->cfg.secure;
}

static void virt_set_secure(Object *obj, bool value, Error **errp)
{
    SigiVirt *s = SIGI_VIRT(obj);

    s->cfg.secure = value;
}

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

static void create_remote_port(SigiVirt *s)
{
    // MemoryRegion *sysmem = get_system_memory();
    // hwaddr base = base_memmap[VIRT_REMOTER_PORT].base;
    // hwaddr size = base_memmap[VIRT_REMOTER_PORT].size;
    // SysBusDevice *sbd;
    DeviceState *dev = NULL;
    // DeviceClass *dc;

    object_initialize_child(OBJECT(s), "cosim-rp", &s->apu.rp,
                            TYPE_REMOTE_PORT);
    dev = DEVICE(&s->apu.rp);
    qdev_prop_set_string(dev, "chrdev-id", "cosim");
    object_property_set_bool(OBJECT(dev), "sync", true, &error_fatal);
    if (global_sync_quantum)
        qdev_prop_set_uint32(dev, "sync-quantum", global_sync_quantum);

    // object_initialize_child(OBJECT(s), "cosim-rpmm", &s->apu.rpmm,
    //                         TYPE_REMOTE_PORT_MEMORY_MASTER);

    // dev = DEVICE(&s->apu.rpmm);
    // qdev_prop_set_uint32(dev, "map-num", 1);
    // qdev_prop_set_uint64(dev, "map-offset", base);
    // qdev_prop_set_uint64(dev, "map-size", size);
    // qdev_prop_set_uint32(dev, "rp-chan0", 9);

    // object_property_set_link(OBJECT(dev), "rp-adaptor0", OBJECT(&s->apu.rp), &error_abort);
    // object_property_set_link(OBJECT(&s->apu.rp), "remote-port-dev9", OBJECT(dev), &error_abort);

    // object_property_set_bool(OBJECT(&s->apu.rp), "realized", true, &error_fatal);
    // dc = DEVICE_GET_CLASS(DEVICE(&s->apu.rp));
    // if (dc->reset) {
    //     /*
    //      * RP adaptors don't connect to busses that reset them,
    //      * manually register the handler.
    //      */
    //     qemu_register_reset((void (*)(void *))dc->reset, OBJECT(&s->apu.rp));
    // }

    // sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

    // /* Connect things to the machine.  */
    // sbd = SYS_BUS_DEVICE(dev);
    // memory_region_add_subregion(sysmem, base, sysbus_mmio_get_region(sbd, 0));
}

static void create_remote_port_net(SigiVirt *s)
{
    MemoryRegion *sysmem = get_system_memory();
    hwaddr base = base_memmap[VIRT_REMOTER_PORT_NET].base;
    hwaddr size = base_memmap[VIRT_REMOTER_PORT_NET].size;
	int irq = a78irqmap[VIRT_REMOTER_PORT_NET];
    SysBusDevice *sbd;
    DeviceState *dev = NULL;
    DeviceClass *dc;
	int i;

    object_initialize_child(OBJECT(s), "cosim-rpmm-net", &s->apu.rpmm_net,
                            TYPE_REMOTE_PORT_MEMORY_MASTER);

    dev = DEVICE(&s->apu.rpmm_net);
    qdev_prop_set_uint32(dev, "map-num", 1);
    qdev_prop_set_uint64(dev, "map-offset", base);
    qdev_prop_set_uint64(dev, "map-size", size);
    qdev_prop_set_uint32(dev, "rp-chan0", 9);

    object_property_set_link(OBJECT(dev), "rp-adaptor0", OBJECT(&s->apu.rp), &error_abort);
    object_property_set_link(OBJECT(&s->apu.rp), "remote-port-dev9", OBJECT(dev), &error_abort);

	object_initialize_child(OBJECT(s), "cosim-rpms-net", &s->apu.rpms_net,
							TYPE_REMOTE_PORT_MEMORY_SLAVE);
	dev = DEVICE(&s->apu.rpms_net);
	object_property_set_link(OBJECT(dev), "rp-adaptor0", OBJECT(&s->apu.rp), &error_fatal);
	object_property_set_link(OBJECT(&s->apu.rp), "remote-port-dev0", OBJECT(dev), &error_fatal);

	object_initialize_child(OBJECT(s), "cosim-rp-irq", &s->apu.rpirq,
							TYPE_REMOTE_PORT_GPIO);
	dev = DEVICE(&s->apu.rpirq);
	object_property_set_link(OBJECT(dev), "rp-adaptor0", OBJECT(&s->apu.rp), &error_fatal);
	object_property_set_link(OBJECT(&s->apu.rp), "remote-port-dev12", OBJECT(dev), &error_fatal);

    object_property_set_bool(OBJECT(&s->apu.rp), "realized", true, &error_fatal);
    dc = DEVICE_GET_CLASS(DEVICE(&s->apu.rp));
    if (dc->reset) {
        /*
         * RP adaptors don't connect to busses that reset them,
         * manually register the handler.
         */
        qemu_register_reset((void (*)(void *))dc->reset, OBJECT(&s->apu.rp));
    }

	qdev_realize(DEVICE(&s->apu.rpms_net), NULL, &error_fatal);
	sysbus_realize(SYS_BUS_DEVICE(&s->apu.rpmm_net), &error_fatal);
	sysbus_realize(SYS_BUS_DEVICE(&s->apu.rpirq), &error_fatal);

	/* Connect things to the machine.  */
	sbd = SYS_BUS_DEVICE(&s->apu.rpmm_net);
	memory_region_add_subregion(sysmem, base, sysbus_mmio_get_region(sbd, 0));

	for (i = 0; i < 5; i++) {
		qemu_irq qirq = qdev_get_gpio_in(DEVICE(&s->apu.gic), irq + i);
		sysbus_connect_irq(SYS_BUS_DEVICE(&s->apu.rpirq), i, qirq);
	}
}

static void create_usb(SigiVirt *s, int usb)
{
    MemoryRegion *sysmem = get_system_memory();
    int irq = a78irqmap[usb];
    hwaddr base = base_memmap[usb].base;
    DeviceState *gicdev = DEVICE(&s->apu.gic);
    DeviceState *dev;
    MemoryRegion *mr;
    USBDWC3 *usbc;

    object_initialize_child(OBJECT(s), "usb", &s->apu.peri.usb,
                            TYPE_USB_DWC3);
    usbc = &s->apu.peri.usb;
    dev = DEVICE(usbc);

    qdev_prop_set_uint32(dev, "intrs", 1);
    qdev_prop_set_uint32(dev, "slots", 2);
    sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion(sysmem, base, mr);

    sysbus_connect_irq(SYS_BUS_DEVICE(&usbc->sysbus_xhci), 0,
                            qdev_get_gpio_in(gicdev, irq));
}

static void create_emmc_card(SigiVirt *s, CadenceSDHCIState *cdns, int index)
{
    DriveInfo *di = drive_get(IF_EMMC, 0, index);
    BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;
    DeviceState *emmc;

    emmc = qdev_new(TYPE_EMMC);
    emmc->id = g_strdup_printf("emmc%d", index);
    object_property_add_child(OBJECT(cdns), "emmc[*]", OBJECT(emmc));
    object_property_set_uint(OBJECT(emmc), "spec_version", 3, &error_fatal);
    object_property_set_uint(OBJECT(emmc), "boot-config", s->cfg.part_config, &error_fatal);
    qdev_prop_set_drive_err(emmc, "drive", blk, &error_fatal);
    qdev_realize_and_unref(emmc, cdns->bus, &error_fatal);
}

static void create_sd_card(CadenceSDHCIState *cdns, int index)
{
    DriveInfo *di = drive_get(IF_SD, 0, index);
    BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;
    DeviceState *card;

    card = qdev_new(TYPE_SD_CARD);
    card->id = g_strdup_printf("sd%d", index);
    object_property_add_child(OBJECT(cdns), "card[*]", OBJECT(card));
    qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
    qdev_realize_and_unref(card, cdns->bus, &error_fatal);
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

static void create_i2c(SigiVirt *s, int i2c)
{
    MemoryRegion *sysmem = get_system_memory();
    int irq = a78irqmap[i2c];
    hwaddr base = base_memmap[i2c].base;
    hwaddr size = base_memmap[i2c].size;
    DeviceState *gicdev = DEVICE(&s->apu.gic);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->apu.peri.i2c); i++) {
        DeviceState *dev;
        MemoryRegion *mr;

        object_initialize_child(OBJECT(s), "i2c[*]", &s->apu.peri.i2c[i],
                                TYPE_DWAPB_I2C);
        dev = DEVICE(&s->apu.peri.i2c[i]);
        dev->id = g_strdup_printf("i2c%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(sysmem, base, mr);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(gicdev, irq));

        base += size;
        irq++;
    }
}

static void create_its(SigiVirt *s)
{
    const char *itsclass = its_class_name();
    DeviceState *dev;

    if (strcmp(itsclass, "arm-gicv3-its")) {
            itsclass = NULL;
    }

    if (!itsclass) {
        /* Do nothing if not supported */
        return;
    }

    object_initialize_child(OBJECT(s), "gic-its", &s->apu.its, itsclass);
    dev = DEVICE(&s->apu.its);

    object_property_set_link(OBJECT(dev), "parent-gicv3", OBJECT(&s->apu.gic),
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base_memmap[VIRT_GIC_ITS].base);
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

    create_its(s);
}

static void create_pcie(SigiVirt *s, int pcie)
{
    MemoryRegion *sysmem = get_system_memory();
    DeviceState *gicdev = DEVICE(&s->apu.gic);
    int irq = a78irqmap[pcie];
    DeviceState *dev;
    MemoryRegion *mmio_alias;
    MemoryRegion *mmio_reg;
    MemoryRegion *ecam_alias;
    MemoryRegion *ecam_reg;
    int i;

    object_initialize_child(OBJECT(s), "pcie", &s->apu.peri.pcie,
                                TYPE_GPEX_HOST);
    dev = DEVICE(&s->apu.peri.pcie);
    sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

    /* Map only the first size_ecam bytes of ECAM space */
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, base_memmap[VIRT_PCIE_ECAM].size);
    memory_region_add_subregion(sysmem, base_memmap[VIRT_PCIE_ECAM].base,
                                    ecam_alias);

    /* Map the MMIO window into system address space so as to expose
     * the section of PCI MMIO space which starts at the same base address
     * (ie 1:1 mapping for that part of PCI MMIO space visible through
     * the window).
     */
    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                            mmio_reg, base_memmap[VIRT_PCIE_MMIO].base,
                            base_memmap[VIRT_PCIE_MMIO].size);
    memory_region_add_subregion(get_system_memory(), base_memmap[VIRT_PCIE_MMIO].base, mmio_alias);

    /* Map high MMIO space */
    MemoryRegion *high_mmio_alias = g_new0(MemoryRegion, 1);
    memory_region_init_alias(high_mmio_alias, OBJECT(dev), "pcie-mmio-high",
                            mmio_reg, base_memmap[VIRT_PCIE_MMIO_HIGH].base,
                            base_memmap[VIRT_PCIE_MMIO_HIGH].size);
    memory_region_add_subregion(get_system_memory(), base_memmap[VIRT_PCIE_MMIO_HIGH].base,
                                high_mmio_alias);

    /* Map IO port space */
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, base_memmap[VIRT_PCIE_PIO].base);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                           qdev_get_gpio_in(gicdev, irq + i));
        gpex_set_irq_num(GPEX_HOST(dev), i, irq + i);
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

        if (!s->cfg.secure)
            object_property_set_bool(cpuobj, "has_el3", false, NULL);

        if (!s->cfg.virt)
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
    hwaddr interleave_base = base_memmap[VIRT_INTERLEVEL_MEM].base;
    uint64_t offset = 0;
    char *name;
    uint64_t mapsize;

    mapsize = cfg_ddr_size < size ? cfg_ddr_size : size;
    name = g_strdup_printf("sigi-ddr");
    /* Create the MR alias.  */
    memory_region_init_alias(&s->mr_non_interleave_ddr, OBJECT(s),
                                name, s->cfg.mr_ddr,
                                offset, mapsize);

    name = g_strdup_printf("sigi-interleave-ddr");
    memory_region_init_alias(&s->mr_interleave_ddr, OBJECT(s),
                                name, s->cfg.mr_ddr,
                                offset, mapsize);

    /* Map it onto the main system MR.  */
    memory_region_add_subregion(sysmem, base, &s->mr_non_interleave_ddr);
    memory_region_add_subregion(sysmem, interleave_base, &s->mr_interleave_ddr);
    g_free(name);

    /* Create the On Chip Memory (L2SRAM).  */
    memory_region_init_ram(&s->mr_l2sram, OBJECT(s), "l2sram",
                           base_memmap[VIRT_L2SRAM].size, &error_fatal);
    memory_region_add_subregion_overlap(sysmem, base_memmap[VIRT_L2SRAM].base, &s->mr_l2sram, 0);

    /* Create Low DDR memory. */
    memory_region_init_ram(&s->mr_low_ddr, OBJECT(s), "low_ddr",
                           base_memmap[VIRT_LOW_MEM].size, &error_fatal);
    memory_region_add_subregion_overlap(sysmem, base_memmap[VIRT_LOW_MEM].base, &s->mr_low_ddr, 0);
}

static void create_unimp(SigiVirt *s)
{
    create_unimplemented_device("peri-sysreg", 0x39010000, 0x10000);
}

static void create_pmu(SigiVirt *s, int pmu)
{
    MemoryRegion *sysmem = get_system_memory();
    hwaddr base = base_memmap[pmu].base;
    DeviceState *dev;
    MemoryRegion *mr;

    object_initialize_child(OBJECT(s), "pmu", &s->pmu, TYPE_SIGI_PMU);

    dev = DEVICE(&s->pmu);
    object_property_set_link(OBJECT(dev), "shared-ocm", OBJECT(&s->mr_l2sram),
                                    &error_abort);

    sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_add_subregion(sysmem, base, mr);
}

#define VIRT_FLASH_SECTOR_SIZE (256 * KiB)

static PFlashCFI01 *virt_flash_create1(SigiVirt *s,
                                        const char *name,
                                        const char *alias_prop_name)
{
    /*
     * Create a single flash device.  We use the same parameters as
     * the flash devices on the Versatile Express board.
     */
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);
    //object_property_add_child(OBJECT(s), "flash[*]", OBJECT(dev));

    qdev_prop_set_uint64(dev, "sector-length", VIRT_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 4);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", name);
    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    object_property_add_alias(OBJECT(s), alias_prop_name,
                              OBJECT(dev), "drive");
    return PFLASH_CFI01(dev);
}

static void create_cfi_flash(SigiVirt *s)
{
    s->flash[0] = virt_flash_create1(s, "virt.flash0", "pflash0");
    s->flash[1] = virt_flash_create1(s, "virt.flash1", "pflash1");
}

static void virt_flash_map1(PFlashCFI01 *flash,
                            hwaddr base, hwaddr size,
                            MemoryRegion *sysmem)
{
    DeviceState *dev = DEVICE(flash);

    assert(QEMU_IS_ALIGNED(size, VIRT_FLASH_SECTOR_SIZE));
    assert(size / VIRT_FLASH_SECTOR_SIZE <= UINT32_MAX);
    qdev_prop_set_uint32(dev, "num-blocks", size / VIRT_FLASH_SECTOR_SIZE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    memory_region_add_subregion(sysmem, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev),
                                                       0));
}

static void create_flash_memmap(SigiVirt *s)
{
    MemoryRegion *sysmem = get_system_memory();
    /*
     * Map two flash devices to fill the VIRT_FLASH space in the memmap.
     * sysmem is the system memory space.
     */
    hwaddr flashsize = base_memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = base_memmap[VIRT_FLASH].base;

    virt_flash_map1(s->flash[0], flashbase, flashsize,
                    sysmem);
    virt_flash_map1(s->flash[1], flashbase + flashsize, flashsize,
                    sysmem);
}

static void sigi_virt_realize(DeviceState *dev, Error **errp)
{
    SigiVirt *s = SIGI_VIRT(dev);
    int i;

    create_apu(s);
    create_gic(s);
    create_uart(s, VIRT_UART);
    create_sdhci(s, VIRT_SDHCI);
    create_gpio(s, VIRT_GPIO);
    create_pcie(s, VIRT_PCIE_ECAM);
    create_usb(s, VIRT_DWC_USB);
    create_i2c(s, VIRT_I2C);
    create_ddr_memmap(s, VIRT_MEM);
    create_pmu(s, VIRT_PMU);
    create_cfi_flash(s);
    create_unimp(s);

    for (i = 0; i < ARRAY_SIZE(s->apu.peri.mmc); i++) {
        if (s->cfg.has_emmc && i == 0) {
            create_emmc_card(s, &s->apu.peri.mmc[i], i);
            continue;
        }
        create_sd_card(&s->apu.peri.mmc[i], i);
    }

    /* Map legacy -drive if=pflash to machine properties */
    for (i = 0; i < ARRAY_SIZE(s->flash); i++) {
        pflash_cfi01_legacy_drive(s->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
    }
    create_flash_memmap(s);

    if (rp_path) {
		create_remote_port(s);
		create_remote_port_net(s);
	}
}

static Property sigi_virt_properties[] = {
    DEFINE_PROP_LINK("sigi-virt.ddr", SigiVirt, cfg.mr_ddr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_BOOL("has-emmc", SigiVirt, cfg.has_emmc, false),
    DEFINE_PROP_UINT8("part-config", SigiVirt, cfg.part_config, 0x0),
    DEFINE_PROP_END_OF_LIST()
};

static void sigi_virt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sigi_virt_realize;
    device_class_set_props(dc, sigi_virt_properties);

    object_class_property_add_bool(klass, "virtualization", sigi_virt_get_virt,
                                   sigi_virt_set_virt);
    object_class_property_set_description(klass, "virtualization",
                                            "Set on/off to enable/disable emulating a "
                                            "guest CPU which implements the ARM "
                                            "Virtualization Extensions");
    object_class_property_add_bool(klass, "secure", virt_get_secure,
                                    virt_set_secure);
    object_class_property_set_description(klass, "secure",
                                            "Set on/off to enable/disable the ARM "
                                            "Security Extensions (TrustZone)");
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
