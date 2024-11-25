/*
 * Laguna SoC emulation
 *
 * Copyright (C) 2024 Charleye <wangkart@aliyun.com>
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
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "sysemu/hostmem.h"
#include "migration/vmstate.h"
#include "sysemu/reset.h"
#include "sysemu/blockdev.h"
#include "hw/arm/laguna.h"
#include "hw/ssi/ssi.h"
#include "hw/irq.h"

static bool lua_soc_get_virt(Object *obj, Error **errp)
{
	LagunaSoC *s = LUA_SOC(obj);

	return s->cfg.virt;
}

static void lua_soc_set_virt(Object *obj, bool value, Error **errp)
{
	LagunaSoC *s = LUA_SOC(obj);

	s->cfg.virt = value;
}

static bool lua_soc_get_secure(Object *obj, Error **errp)
{
	LagunaSoC *s = LUA_SOC(obj);

	return s->cfg.secure;
}

static void lua_soc_set_secure(Object *obj, bool value, Error **errp)
{
	LagunaSoC *s = LUA_SOC(obj);

	s->cfg.secure = value;
}

uint8_t start_powered_off;
static void create_apu(LagunaSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int i;

	for (i = 0; i < ARRAY_SIZE(s->apu.cpus); i++) {
		Object *cpuobj;

		object_initialize_child(OBJECT(s), "apu[*]", &s->apu.cpus[i],
									LUA_SOC_ACPU_TYPE);
		cpuobj = OBJECT(&s->apu.cpus[i]);
		if (i) {
			/* Secondary CPUs start in powered-down state */
			object_property_set_bool(cpuobj, "start-powered-off", true,
										&error_abort);
			start_powered_off |= BIT(i);
		}

		object_property_set_int(cpuobj, "mp-affinity",
								lua_cpu_mp_affinity(i), NULL);

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

static void create_a55_ctrl(LagunaSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	hwaddr base = base_memmap[VIRT_A55_CTRL].base;
	DeviceState *dev;
	MemoryRegion *mr;

	object_initialize_child(OBJECT(s), "a55_cpu_ctrl", &s->apu.cc,
	                           TYPE_LUA_CORE_CTRL);
	dev = DEVICE(&s->apu.cc);
	qdev_prop_set_uint32(dev, "start-powered-off", start_powered_off);
	sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);
	mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
	memory_region_add_subregion(sysmem, base, mr);
}

static void create_gic(LagunaSoC *s)
{
	int nr_apu = ARRAY_SIZE(s->apu.cpus);
	/* We create a standalone GIC */
	SysBusDevice *gicbusdev;
	DeviceState *gicdev;
	int i;

	object_initialize_child(OBJECT(s), "apu-gic", &s->apu.gic, TYPE_ARM_GIC);
	gicdev = DEVICE(&s->apu.gic);
	qdev_prop_set_uint32(gicdev, "revision", 2);
	qdev_prop_set_uint32(gicdev, "num-cpu", nr_apu);
	/* Note that the num-irq property counts both internal and external
		* interrupts; there are always 32 of the former (mandated by GIC spec).
		*/
	qdev_prop_set_uint32(gicdev, "num-irq",
							LUA_SOC_NUM_IRQS + 32);
	qdev_prop_set_bit(gicdev, "has-security-extensions", s->cfg.secure);
	qdev_prop_set_bit(gicdev, "has-virtualization-extensions", s->cfg.virt);

	gicbusdev = SYS_BUS_DEVICE(gicdev);
	sysbus_realize(gicbusdev, &error_fatal);
	sysbus_mmio_map(gicbusdev, 0, base_memmap[VIRT_GIC_DIST].base);
	sysbus_mmio_map(gicbusdev, 1, base_memmap[VIRT_GIC_CPU].base);
	if (s->cfg.virt) {
		sysbus_mmio_map(gicbusdev, 2, base_memmap[VIRT_GIC_HYP].base);
		sysbus_mmio_map(gicbusdev, 3, base_memmap[VIRT_GIC_VCPU].base);
	}

	/* Wire the outputs from each CPU's generic timer and the GICv3
		* maintenance interrupt signal to the appropriate GIC PPI inputs,
		* and the GIC's IRQ/FIQ/VIRQ/VFIQ interrupt outputs to the CPU's inputs.
		*/
	for (i = 0; i < nr_apu; i++) {
		DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
		int ppibase = LUA_SOC_NUM_IRQS + i * GIC_INTERNAL + GIC_NR_SGIS;
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

		if (s->cfg.virt) {
			qemu_irq irq_in = qdev_get_gpio_in(gicdev,
											ppibase + ARCH_GIC_MAINT_IRQ);
			sysbus_connect_irq(gicbusdev, i + 4 * nr_apu, irq_in);
		}
		qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
						qdev_get_gpio_in(gicdev, ppibase + ARCH_VITRUAL_PMU_IRQ));

		sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
		sysbus_connect_irq(gicbusdev, i + nr_apu,
							qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
		sysbus_connect_irq(gicbusdev, i + 2 * nr_apu,
							qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
		sysbus_connect_irq(gicbusdev, i + 3 * nr_apu,
							qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
	}
}

static void create_gpio(LagunaSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = apu_irqmap[VIRT_GPIO];
	hwaddr base = base_memmap[VIRT_GPIO].base;
	hwaddr size = base_memmap[VIRT_GPIO].size;
	DeviceState *gicdev = DEVICE(&s->apu.gic);
	int i;

	for (i = 0; i < ARRAY_SIZE(s->apu.peri.gpios); i++) {
		char *name = g_strdup_printf("gpio%d", i);
		DeviceState *dev;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), name, &s->apu.peri.gpios[i],
								TYPE_DWAPB_GPIO);
		dev = DEVICE(&s->apu.peri.gpios[i]);
		dev->id = g_strdup_printf("gpio%d", i);
		sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

		mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
		memory_region_add_subregion(sysmem, base, mr);

		sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
							qdev_get_gpio_in(gicdev, irq));

		base += size;
		irq += 2;
		g_free(name);
	}
}

static void create_uart0(LagunaSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = apu_irqmap[VIRT_SAFETY_UART0];
	hwaddr base = base_memmap[VIRT_SAFETY_UART0].base;
	DeviceState *gicdev = DEVICE(&s->apu.gic);
	const char *name = "safety_uart0";
	DeviceState *dev;
	MemoryRegion *mr;
	int i;

	i = s->cfg.match ? 0 : 5;
	object_initialize_child(OBJECT(s), name, &s->apu.peri.uarts[i],
							TYPE_DWC_UART);
	dev = DEVICE(&s->apu.peri.uarts[i]);
	qdev_prop_set_uint8(dev, "regshift", 2);
	qdev_prop_set_uint32(dev, "baudbase", 115200);
	qdev_prop_set_uint8(dev, "endianness", DEVICE_LITTLE_ENDIAN);
	qdev_prop_set_chr(dev, "chardev", serial_hd(i));
	qdev_prop_set_uint8(dev, "index", i);
	sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

	mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
	memory_region_add_subregion(sysmem, base, mr);

	sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(gicdev, irq));
}

static void create_uart4(LagunaSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = apu_irqmap[VIRT_UART4];
	hwaddr base = base_memmap[VIRT_UART4].base;
	hwaddr size = base_memmap[VIRT_UART4].size;
	DeviceState *gicdev = DEVICE(&s->apu.gic);
	int i, limit;

	i = s->cfg.match ? 4 : 3;
	limit = s->cfg.match ? 6 : 5;
	for (; i < limit; i++) {
		char *name = g_strdup_printf("uart%d", i);
		DeviceState *dev;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), name, &s->apu.peri.uarts[i],
								TYPE_DWC_UART);
		dev = DEVICE(&s->apu.peri.uarts[i]);
		qdev_prop_set_uint8(dev, "regshift", 2);
		qdev_prop_set_uint32(dev, "baudbase", 115200);
		qdev_prop_set_uint8(dev, "endianness", DEVICE_LITTLE_ENDIAN);
		qdev_prop_set_chr(dev, "chardev", serial_hd(i));
		qdev_prop_set_uint8(dev, "index", i);
		sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

		mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
		memory_region_add_subregion(sysmem, base, mr);

		sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(gicdev, irq));

		base += size;
		irq += 1;
		g_free(name);
	}
}

static void create_uart1(LagunaSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = apu_irqmap[VIRT_UART1];
	hwaddr base = base_memmap[VIRT_UART1].base;
	hwaddr size = base_memmap[VIRT_UART1].size;
	DeviceState *gicdev = DEVICE(&s->apu.gic);
	int i, limit;

	i = s->cfg.match ? 1 : 0;
	limit = s->cfg.match ? 4 : 3;
	for (; i < limit; i++) {
		char *name = g_strdup_printf("uart%d", i);
		DeviceState *dev;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), name, &s->apu.peri.uarts[i],
								TYPE_DWC_UART);
		dev = DEVICE(&s->apu.peri.uarts[i]);
		qdev_prop_set_uint8(dev, "regshift", 2);
		qdev_prop_set_uint32(dev, "baudbase", 115200);
		qdev_prop_set_uint8(dev, "endianness", DEVICE_LITTLE_ENDIAN);
		qdev_prop_set_chr(dev, "chardev", serial_hd(i));
		qdev_prop_set_uint8(dev, "index", i);
		sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

		mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
		memory_region_add_subregion(sysmem, base, mr);

		sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(gicdev, irq));

		base += size;
		irq += 1;
		g_free(name);
	}
}

static void create_uart(LagunaSoC *s)
{
	create_uart0(s);
	create_uart1(s);
	create_uart4(s);
}

static bool nor_flash_valid(const char *model)
{
	GSList *list, *elt;

	list = object_class_get_list_sorted(TYPE_DEVICE, false);

	for (elt = list; elt; elt = elt->next) {
		DeviceClass *dc = OBJECT_CLASS_CHECK(DeviceClass, elt->data, TYPE_DEVICE);
		const char *name = object_class_get_name(OBJECT_CLASS(dc));

		if (!dc->bus_type || strncmp("SSI", dc->bus_type, 3))
			continue;

		if (strcmp(name, model) == 0)
			return true;
	}

	return false;
}

static DeviceState* create_nor_flash(LagunaSoC *s, int unit)
{
	static DeviceState *nor_flash;
	DriveInfo *dinfo = drive_get(IF_MTD, 0, unit);

	if (!dinfo)
		return NULL;

	if (!nor_flash_valid(s->cfg.nor_flash)) {
		error_report("Flash model %s not supported", s->cfg.nor_flash);
		exit(1);
	}

	nor_flash = qdev_new(s->cfg.nor_flash);
	qdev_prop_set_drive_err(nor_flash, "drive",
			blk_by_legacy_dinfo(dinfo), &error_fatal);

	return nor_flash;
}

static DeviceState* create_nand_flash(LagunaSoC *s, int unit)
{
	static DeviceState *nand;
	DriveInfo *dinfo = drive_get(IF_MTD, 0, unit);

	if (!dinfo)
		return NULL;

	nand = qdev_new("TC58CVG2S0HRAIG");
	qdev_prop_set_drive_err(nand, "drive",
			blk_by_legacy_dinfo(dinfo), &error_fatal);

	return nand;
}

static void create_qspi_flash(LagunaSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = apu_irqmap[VIRT_SAFETY_QSPI];
	hwaddr base = base_memmap[VIRT_SAFETY_QSPI].base;
	hwaddr size = base_memmap[VIRT_SAFETY_QSPI].size;
	DeviceState *gicdev = DEVICE(&s->apu.gic);
	DeviceState *nor_dev, *nand_dev;
	BusState *spi_bus;
	qemu_irq cs_line;
	const int flash_num = 2;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(s->apu.peri.qspi); i++) {
		char *name = g_strdup_printf("qspi%d", i);
		DeviceState *dev;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), name, &s->apu.peri.qspi[i],
								TYPE_DESIGNWARE_SPI);
		dev = DEVICE(&s->apu.peri.qspi[i]);
		qdev_prop_set_uint32(dev, "num-cs", flash_num);
		qdev_prop_set_uint32(dev, "len-flash-dev", flash_num);
		for (j = 0; j < flash_num; j++) {
			char *propname = g_strdup_printf("flash-dev[%d]", j);
			if (j)
				nand_dev = create_nand_flash(s, j + 2);
			else
				nor_dev = create_nor_flash(s, j + 2);
			qdev_prop_set_uint64(dev, propname, (uint64_t)(j ? nand_dev : nor_dev));
			g_free(propname);
		}

		sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

		mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
		memory_region_add_subregion(sysmem, base, mr);

		sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
		                                qdev_get_gpio_in(gicdev, irq));
		spi_bus = BUS(s->apu.peri.qspi[i].spi);
		base += size;
		irq += 1;
		g_free(name);

		/* nor flash memory */
		if (nor_dev) {
			qdev_realize_and_unref(nor_dev, spi_bus, &error_fatal);
			cs_line = qdev_get_gpio_in_named(nor_dev, SSI_GPIO_CS, 0);
			sysbus_connect_irq(SYS_BUS_DEVICE(&s->apu.peri.qspi[i]), 1, cs_line);
		}

		/* nand flash memory */
		if (nand_dev) {
			qdev_realize_and_unref(nand_dev, spi_bus, &error_fatal);
			cs_line = qdev_get_gpio_in_named(nand_dev, SSI_GPIO_CS, 0);
			sysbus_connect_irq(SYS_BUS_DEVICE(&s->apu.peri.qspi[i]), 2, cs_line);
		}
	}
}

static void create_ospi_flash(LagunaSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = apu_irqmap[VIRT_OSPI];
	hwaddr base = base_memmap[VIRT_OSPI].base;
	hwaddr size = base_memmap[VIRT_OSPI].size;
	DeviceState *gicdev = DEVICE(&s->apu.gic);
	DeviceState *nor_dev, *nand_dev;
	BusState *spi_bus;
	qemu_irq cs_line;
	const int flash_num = 2;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(s->apu.peri.ospi); i++) {
		char *name = g_strdup_printf("ospi%d", i);
		DeviceState *dev;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), name, &s->apu.peri.ospi[i],
								TYPE_DESIGNWARE_SPI);
		dev = DEVICE(&s->apu.peri.ospi[i]);
		qdev_prop_set_uint32(dev, "num-cs", flash_num);
		qdev_prop_set_uint32(dev, "len-flash-dev", flash_num);
		for (j = 0; j < flash_num; j++) {
			char *propname = g_strdup_printf("flash-dev[%d]", j);
			if (j)
				nand_dev = create_nand_flash(s, j);
			else
				nor_dev = create_nor_flash(s, j);
			qdev_prop_set_uint64(dev, propname, (uint64_t)(j ? nand_dev : nor_dev));
			g_free(propname);
		}

		sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

		mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
		memory_region_add_subregion(sysmem, base, mr);

		sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
		                                qdev_get_gpio_in(gicdev, irq));
		spi_bus = BUS(s->apu.peri.ospi[i].spi);
		base += size;
		irq += 1;
		g_free(name);

		/* nor flash memory */
		if (nor_dev) {
			qdev_realize_and_unref(nor_dev, spi_bus, &error_fatal);
			cs_line = qdev_get_gpio_in_named(nor_dev, SSI_GPIO_CS, 0);
			sysbus_connect_irq(SYS_BUS_DEVICE(&s->apu.peri.ospi[i]), 1, cs_line);
		}

		/* nand flash memory */
		if (nand_dev) {
			qdev_realize_and_unref(nand_dev, spi_bus, &error_fatal);
			cs_line = qdev_get_gpio_in_named(nand_dev, SSI_GPIO_CS, 0);
			sysbus_connect_irq(SYS_BUS_DEVICE(&s->apu.peri.ospi[i]), 2, cs_line);
		}
	}
}

static void create_ethernet(LagunaSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = apu_irqmap[VIRT_EMAC];
	hwaddr base = base_memmap[VIRT_EMAC].base;
	DeviceState *gicdev = DEVICE(&s->apu.gic);
	char *name = g_strdup_printf("eth%d", 0);
	DeviceState *dev;
	MemoryRegion *mr;

	object_initialize_child(OBJECT(s), name, &s->apu.peri.eqos, TYPE_DWC_ETHER_QOS);
	dev = DEVICE(&s->apu.peri.eqos);
	if (nd_table[0].used) {
		qemu_check_nic_model(&nd_table[0], TYPE_DWC_ETHER_QOS);
		qdev_set_nic_properties(dev, &nd_table[0]);
	}
	qdev_prop_set_uint8(dev, "phy-addr", 1);
	sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);
	mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
	memory_region_add_subregion(sysmem, base, mr);
	sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(gicdev, irq));
}

static void create_usb(LagunaSoC *s)
{
    MemoryRegion *sysmem = get_system_memory();
    int irq = apu_irqmap[VIRT_USB];
    hwaddr base = base_memmap[VIRT_USB].base;
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

static void create_emmc(LagunaSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = apu_irqmap[VIRT_EMMC];
	hwaddr base = base_memmap[VIRT_EMMC].base;
	hwaddr size = base_memmap[VIRT_EMMC].size;
	DeviceState *gicdev = DEVICE(&s->apu.gic);
	int i;

	for (i = 0; i < ARRAY_SIZE(s->apu.peri.mmc); i++) {
		char *name = g_strdup_printf("sdhci%d", i);
		DeviceState *dev;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), name, &s->apu.peri.mmc[i],
								TYPE_SYSBUS_SDHCI);
		dev = DEVICE(&s->apu.peri.mmc[i]);
		object_property_set_uint(OBJECT(dev), "sd-spec-version", 3, &error_fatal);
		object_property_set_uint(OBJECT(dev), "capareg", 0x70156ecc02UL, &error_fatal);
		sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

		mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
		memory_region_add_subregion(sysmem, base, mr);

		sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(gicdev, irq));

		base += size;
		irq += 1;
		g_free(name);
	}
}

static void create_emmc_card(LagunaSoC *s, SDHCIState *mmc, int index)
{
	DriveInfo *di = drive_get(IF_EMMC, 0, index);
	BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;
	DeviceState *emmc;

	emmc = qdev_new(TYPE_EMMC);
	emmc->id = g_strdup_printf("emmc%d", index);
	object_property_add_child(OBJECT(mmc), "emmc[*]", OBJECT(emmc));
	object_property_set_uint(OBJECT(emmc), "spec_version", 3, &error_fatal);
	object_property_set_uint(OBJECT(emmc), "boot-config", s->cfg.part_config, &error_fatal);
	qdev_prop_set_drive_err(emmc, "drive", blk, &error_fatal);
	qdev_realize_and_unref(emmc, BUS(&mmc->sdbus), &error_fatal);
}

static void create_sd_card(SDHCIState *sd, int index)
{
	DriveInfo *di = drive_get(IF_SD, 0, index);
	BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;
	DeviceState *card;

	card = qdev_new(TYPE_SD_CARD);
	card->id = g_strdup_printf("sd%d", index);
	object_property_add_child(OBJECT(sd), "card[*]", OBJECT(card));
	qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
	qdev_realize_and_unref(card, BUS(&sd->sdbus), &error_fatal);
}

/* This takes the board allocated linear DDR memory and creates aliases
 * for each split DDR range/aperture on the address map.
 */
static void create_ddr_memmap(LagunaSoC *s)
{
	uint64_t cfg_ddr_size = memory_region_size(s->cfg.mr_ddr);
	MemoryRegion *sysmem = get_system_memory();
	hwaddr base = base_memmap[VIRT_MEM].base;
	hwaddr size = base_memmap[VIRT_MEM].size;
	hwaddr ocm_base = base_memmap[VIRT_OCM_NPU].base;
	hwaddr ocm_size = base_memmap[VIRT_OCM_NPU].size;
	hwaddr iram_base = base_memmap[VIRT_IRAM_SAFETY].base;
	hwaddr iram_size = base_memmap[VIRT_IRAM_SAFETY].size;
	hwaddr ocms_base = base_memmap[VIRT_OCM_SAFETY].base;
	hwaddr ocms_size = base_memmap[VIRT_OCM_SAFETY].size;
	uint64_t offset = 0;
	char *name;
	uint64_t mapsize;

	mapsize = cfg_ddr_size < size ? cfg_ddr_size : size;
	name = g_strdup_printf("lua-ddr");
	/* Create the MR alias.  */
	memory_region_init_alias(&s->mr_ddr, OBJECT(s),
								name, s->cfg.mr_ddr,
								offset, mapsize);

	/* Map it onto the main system MR.  */
	memory_region_add_subregion(sysmem, base, &s->mr_ddr);
	g_free(name);

	memory_region_init_ram(&s->mr_ocm, OBJECT(s), "ocm", ocm_size, &error_fatal);
	memory_region_add_subregion(sysmem, ocm_base, &s->mr_ocm);

	memory_region_init_ram(&s->mr_iram_safety, OBJECT(s), "iram-safety", iram_size, &error_fatal);
	memory_region_add_subregion(sysmem, iram_base, &s->mr_iram_safety);

	/* Map ocm_safety into the main system memory */
	memory_region_init_ram(&s->mr_ocm_safety, OBJECT(s), "ocm-safety", ocms_size, &error_fatal);
	memory_region_add_subregion(sysmem, ocms_base, &s->mr_ocm_safety);

}

static void create_unimp(LagunaSoC *s)
{
	int i;
	char *name;

	for (i = 0; i < ARRAY_SIZE(unimp_memmap); i++) {
		name = g_strdup_printf("unimp_device@%08lx", unimp_memmap[i].base);
		create_unimplemented_device(name, unimp_memmap[i].base, unimp_memmap[i].size);
		g_free(name);
	}
}

static void create_bootmode(LagunaSoC *s)
{
	int i;
	qemu_irq irq;

	for (i = 0; i < LUA_BOOTSTRAP_PINS; i++) {
		irq = qdev_get_gpio_in(DEVICE(&s->apu.peri.gpios[0]), i);
		qdev_connect_gpio_out(DEVICE(s), i, irq);
	}
}

static void create_download(LagunaSoC *s)
{
	qemu_irq irq;

	irq = qdev_get_gpio_in(DEVICE(&s->apu.peri.gpios[0]), 3);
	qdev_connect_gpio_out(DEVICE(s), 3, irq);
}

static void lua_soc_realize(DeviceState *dev, Error **errp)
{
	LagunaSoC *s = LUA_SOC(dev);
	int i;

	create_apu(s);
	create_a55_ctrl(s);
	create_gic(s);
	create_gpio(s);
	create_uart(s);
	create_ethernet(s);
	create_usb(s);
	create_emmc(s);
	create_ospi_flash(s);
	create_qspi_flash(s);
	create_ddr_memmap(s);
	create_unimp(s);


	if (s->cfg.has_emmc)
		create_emmc_card(s, &s->apu.peri.mmc[0], 0);

	i = s->cfg.has_emmc ? 1 : 0;
	for (; i < ARRAY_SIZE(s->apu.peri.mmc); i++) {
		create_sd_card(&s->apu.peri.mmc[i], s->cfg.has_emmc ? (i - 1) : i);
	}

	create_bootmode(s);
	create_download(s);
}

static Property lua_soc_properties[] = {
	DEFINE_PROP_LINK("lua-soc.ddr", LagunaSoC, cfg.mr_ddr, TYPE_MEMORY_REGION,
						MemoryRegion*),
	DEFINE_PROP_BOOL("has-emmc", LagunaSoC, cfg.has_emmc, false),
	DEFINE_PROP_UINT8("part-config", LagunaSoC, cfg.part_config, 0x0),
	DEFINE_PROP_UINT8("bootmode", LagunaSoC, cfg.bootmode, 0x0),
	DEFINE_PROP_STRING("nor-flash", LagunaSoC, cfg.nor_flash),
	DEFINE_PROP_BOOL("download", LagunaSoC, cfg.download, false),
	DEFINE_PROP_BOOL("match", LagunaSoC, cfg.match, false),
	DEFINE_PROP_END_OF_LIST()
};

static void lua_soc_reset(DeviceState *dev)
{
	LagunaSoC *s = LUA_SOC(dev);
	int i;

	for (i = 0; i < LUA_BOOTSTRAP_PINS; i++) {
		if (extract32(s->cfg.bootmode, i, 1))
			qemu_set_irq(s->output[i],
						extract32(s->cfg.bootmode, i, 1));
	}

	if (s->cfg.download)
		qemu_set_irq(s->download, s->cfg.download);
}

static void lua_soc_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	dc->realize = lua_soc_realize;
	device_class_set_props(dc, lua_soc_properties);

	object_class_property_add_bool(klass, "virtualization", lua_soc_get_virt,
									lua_soc_set_virt);
	object_class_property_set_description(klass, "virtualization",
											"Set on/off to enable/disable emulating a "
											"guest CPU which implements the ARM "
											"Virtualization Extensions");
	object_class_property_add_bool(klass, "secure", lua_soc_get_secure,
									lua_soc_set_secure);
	object_class_property_set_description(klass, "secure",
											"Set on/off to enable/disable the ARM "
											"Security Extensions (TrustZone)");

	dc->reset = lua_soc_reset;
}

static void lua_soc_init(Object *obj)
{
	LagunaSoC *s = LUA_SOC(obj);

	qdev_init_gpio_out(DEVICE(s), s->output, LUA_BOOTSTRAP_PINS);
	qdev_init_gpio_out(DEVICE(s), &s->download, 1);
}

static const TypeInfo lua_soc_info = {
	.name = TYPE_LUA_SOC,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LagunaSoC),
	.instance_init = lua_soc_init,
	.class_init = lua_soc_class_init,
};

static void lua_soc_register_types(void)
{
	type_register_static(&lua_soc_info);
}

type_init(lua_soc_register_types);