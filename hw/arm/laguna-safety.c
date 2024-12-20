/*
 * Laguna Safety Island emulation
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
#include "hw/arm/laguna-safety.h"
#include "hw/ssi/ssi.h"

static void create_tcm_slave_lockstep(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	hwaddr tcm0_base = base_memmap[VIRT_CORE0_TCM_SLAVE].base;
	hwaddr tcm0_size = base_memmap[VIRT_CORE0_TCM_SLAVE].size;
	hwaddr tcm1_base = base_memmap[VIRT_CORE1_TCM_SLAVE].base;
	hwaddr tcm1_size = base_memmap[VIRT_CORE1_TCM_SLAVE].size;

	memory_region_init_alias(&s->mr_tcm_slv[0], OBJECT(s), "mcu0_slv_128K", &s->mr_tcm[0], 0x0, tcm0_size >> 1);
	memory_region_add_subregion(sysmem, tcm0_base, &s->mr_tcm_slv[0]);
	create_unimplemented_device("mcu0_slv_bh_128K_rsvd", tcm0_base + (tcm0_size >> 1), tcm1_size >> 1);

	create_unimplemented_device("mcu1_slv_rsvd", tcm1_base, tcm1_size);
}

static void create_apu_lockstep(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	hwaddr tcm_base = base_memmap[VIRT_TCM].base;
	hwaddr tcm_size = base_memmap[VIRT_TCM].size;
	Object *cpuobj;
	const char *name = "tcm0";

	object_initialize_child(OBJECT(s), "mpu[*]", &s->mpu.cpus[0], LUA_SAFETY_MCPU_TYPE);
	cpuobj = OBJECT(&s->mpu.cpus[0]);
	object_property_set_link(cpuobj, "memory", OBJECT(sysmem), &error_abort);
	qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);

	memory_region_init_ram(&s->mr_tcm[0], OBJECT(s), name, 4*tcm_size, &error_fatal);
	memory_region_add_subregion(sysmem, tcm_base, &s->mr_tcm[0]);
}

static void create_tcm_slave_split(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	hwaddr tcm_size = base_memmap[VIRT_TCM].size;
	hwaddr tcm0_base = base_memmap[VIRT_CORE0_TCM_SLAVE].base;
	hwaddr tcm0_size = base_memmap[VIRT_CORE0_TCM_SLAVE].size;
	char *name;
	int i;

	for (i = 0; i < ARRAY_SIZE(s->mpu.cpus); i++) {
		name = g_strdup_printf("mcu%d_slv_tcmA", i);
		memory_region_init_alias(&s->mr_tcm_slv[i], OBJECT(s), name,
						&s->mr_tcm[i], 0x0, tcm_size);
		memory_region_add_subregion(sysmem, tcm0_base + i * tcm0_size,
							&s->mr_tcm_slv[i]);
		g_free(name);

		name = g_strdup_printf("mcu%d_slv_1st_rsvd", i);
		create_unimplemented_device(name, tcm0_base + tcm_size + i * tcm0_size, tcm_size);
		g_free(name);

		name = g_strdup_printf("mcu%d_slv_tcmB", i);
		memory_region_init_alias(&s->mr_tcm_slv[i + 2], OBJECT(s), name,
						&s->mr_tcm[i + 2], 0x0, tcm_size);
		memory_region_add_subregion(sysmem, tcm0_base + tcm_size * 2 + tcm0_size * i,
							&s->mr_tcm_slv[i+2]);
		g_free(name);

		name = g_strdup_printf("mcu%d_slv_2nd_rsvd", i);
		create_unimplemented_device(name, tcm0_base + tcm_size * 3 + i * tcm0_size, tcm_size * 5);
		g_free(name);
	}
}

static void create_apu_split(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	hwaddr tcm_base = base_memmap[VIRT_TCM].base;
	hwaddr tcm_size = base_memmap[VIRT_TCM].size;
	char *name;
	int i;

	for (i = 0; i < ARRAY_SIZE(s->mpu.cpus); i++) {
		Object *cpuobj;

		name = g_strdup_printf("cpu%d-memory", i);
		memory_region_init(&s->mr_cpu[i], OBJECT(s), name, UINT64_MAX);
		g_free(name);

		object_initialize_child(OBJECT(s), "mpu[*]", &s->mpu.cpus[i],
									LUA_SAFETY_MCPU_TYPE);
		cpuobj = OBJECT(&s->mpu.cpus[i]);
		if (i) {
			/* Secondary CPUs start in powered-down state */
			object_property_set_bool(cpuobj, "start-powered-off", true,
										&error_abort);
		}

		object_property_set_link(cpuobj, "memory", OBJECT(&s->mr_cpu[i]),
									&error_abort);

		name = g_strdup_printf("tcm%x_A", i);
		memory_region_init_ram(&s->mr_tcm[i], OBJECT(s), name, tcm_size, &error_fatal);
		memory_region_add_subregion(&s->mr_cpu[i], tcm_base, &s->mr_tcm[i]);
		g_free(name);

		name = g_strdup_printf("tcm%x_B", i);
		memory_region_init_ram(&s->mr_tcm[i + 2], OBJECT(s), name, tcm_size, &error_fatal);
		memory_region_add_subregion(&s->mr_cpu[i], tcm_base + (tcm_size << 1), &s->mr_tcm[i + 2]);
		g_free(name);

		name = g_strdup_printf("cpu%d-alias", i);
		memory_region_init_alias(&s->mr_cpu_alias[i], OBJECT(s), name, sysmem, tcm_size * 4, 0x800000000 - tcm_size * 4);
		g_free(name);

		memory_region_add_subregion_overlap(&s->mr_cpu[i], tcm_size * 4, &s->mr_cpu_alias[i], 0);

		qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
	}
}

static void create_gic(LagunaSafety *s)
{
	int nr_mpu = ARRAY_SIZE(s->mpu.cpus);
	/* We create a standalone GIC */
	SysBusDevice *gicbusdev;
	DeviceState *gicdev;
	int i;

	if (s->cfg.lockstep)
		nr_mpu >>= 1;

	object_initialize_child(OBJECT(s), "mpu-gic", &s->mpu.gic, TYPE_ARM_GIC);
	gicdev = DEVICE(&s->mpu.gic);
	qdev_prop_set_uint32(gicdev, "revision", 2);
	qdev_prop_set_uint32(gicdev, "num-cpu", nr_mpu);
	/* Note that the num-irq property counts both internal and external
	 * interrupts; there are always 32 of the former (mandated by GIC spec).
	 */
	qdev_prop_set_uint32(gicdev, "num-irq",
							LUA_SAFETY_NUM_IRQS + 32);

	gicbusdev = SYS_BUS_DEVICE(gicdev);
	sysbus_realize(gicbusdev, &error_fatal);
	sysbus_mmio_map(gicbusdev, 0, base_memmap[VIRT_GIC1_DIST].base);
	sysbus_mmio_map(gicbusdev, 1, base_memmap[VIRT_GIC1_CPU].base);

	for (i = 0; i < nr_mpu; i++) {
		DeviceState *cpudev = DEVICE(qemu_get_cpu(i));

		sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
		sysbus_connect_irq(gicbusdev, i + nr_mpu,
							qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
	}
}

static void create_uart(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = mpu_irqmap[VIRT_UART];
	hwaddr base = base_memmap[VIRT_UART].base;
	hwaddr size = base_memmap[VIRT_UART].size;
	DeviceState *gicdev = DEVICE(&s->mpu.gic);
	int i;

	for (i = 0; i < ARRAY_SIZE(s->mpu.peri.uarts); i++) {
		char *name = g_strdup_printf("uart%d", i);
		DeviceState *dev;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), name, &s->mpu.peri.uarts[i],
								TYPE_DWC_UART);
		dev = DEVICE(&s->mpu.peri.uarts[i]);
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

static void create_timer(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = mpu_irqmap[VIRT_TIMER];
	hwaddr base = base_memmap[VIRT_TIMER].base;
	hwaddr size = base_memmap[VIRT_TIMER].size;
	DeviceState *gicdev = DEVICE(&s->mpu.gic);
	int i, j;

	for (i = 0; i < ARRAY_SIZE(s->mpu.peri.ttc); i++) {
		SysBusDevice *sbd;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), "ttc[*]", &s->mpu.peri.ttc[i],
									TYPE_CADENCE_TTC);
		sbd = SYS_BUS_DEVICE(&s->mpu.peri.ttc[i]);

		sysbus_realize(sbd, &error_fatal);

		mr = sysbus_mmio_get_region(sbd, 0);
		memory_region_add_subregion(sysmem, base, mr);
		for (j = 0; j < 3; j++) {
			sysbus_connect_irq(sbd, j, qdev_get_gpio_in(gicdev, irq + j));
		}

		base += size;
		irq += (i + 1) * 3;
	}
}

static void create_ethernet(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = mpu_irqmap[VIRT_EMAC];
	hwaddr base = base_memmap[VIRT_EMAC].base;
	DeviceState *gicdev = DEVICE(&s->mpu.gic);
	char *name = g_strdup_printf("eth%d", 0);
	DeviceState *dev;
	MemoryRegion *mr;

	object_initialize_child(OBJECT(s), name, &s->mpu.peri.eqos, TYPE_DWC_ETHER_QOS);
	dev = DEVICE(&s->mpu.peri.eqos);
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

static DeviceState* create_nor_flash(LagunaSafety *s, int unit)
{
	static DeviceState *flash_dev;
	DriveInfo *dinfo = drive_get(IF_MTD, 0, unit);

	if (!nor_flash_valid(s->cfg.nor_flash)) {
		error_report("Flash model %s not supported", s->cfg.nor_flash);
		exit(1);
	}

	flash_dev = qdev_new(s->cfg.nor_flash);
	if (dinfo) {
		qdev_prop_set_drive_err(flash_dev, "drive",
						blk_by_legacy_dinfo(dinfo), &error_fatal);
	}

	return flash_dev;
}


static void create_qspi_nor_flash(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = mpu_irqmap[VIRT_QSPI];
	hwaddr base = base_memmap[VIRT_QSPI].base;
	DeviceState *gicdev = DEVICE(&s->mpu.gic);
	DeviceState *nor_dev;
	BusState *spi_bus;
	qemu_irq cs_line;
	DeviceState *dev;
	MemoryRegion *mr;
	const int flash_num = 1;
	char *name = g_strdup_printf("qspi");

	object_initialize_child(OBJECT(s), name, &s->mpu.peri.qspi,
							TYPE_DESIGNWARE_SPI);
	dev = DEVICE(&s->mpu.peri.qspi);
	qdev_prop_set_uint32(dev, "num-cs", flash_num);
	qdev_prop_set_uint32(dev, "len-flash-dev", flash_num);
	char *propname = g_strdup_printf("flash-dev[0]");
	nor_dev = create_nor_flash(s, 0);
	qdev_prop_set_uint64(dev, propname, (uint64_t)nor_dev);
	g_free(propname);

	sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

	mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
	memory_region_add_subregion(sysmem, base, mr);

	sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
									qdev_get_gpio_in(gicdev, irq));

	g_free(name);

	/* nor flash memory */
	spi_bus = BUS(s->mpu.peri.qspi.spi);
	qdev_realize_and_unref(nor_dev, spi_bus, &error_fatal);
	cs_line = qdev_get_gpio_in_named(nor_dev, SSI_GPIO_CS, 0);
	sysbus_connect_irq(SYS_BUS_DEVICE(&s->mpu.peri.qspi), 1, cs_line);
}

static DeviceState* create_nand_flash(LagunaSafety *s, int unit)
{
	static DeviceState *nand;
	DriveInfo *dinfo = drive_get(IF_MTD, 0, unit);

	nand = qdev_new("TC58CVG2S0HRAIG");
	if (dinfo) {
		qdev_prop_set_drive_err(nand, "drive",
						blk_by_legacy_dinfo(dinfo), &error_fatal);
	}

	return nand;
}

static void create_ospi_nor_nand_flash(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	hwaddr base = base_memmap[VIRT_FLASH_OSPI].base;
	// hwaddr size = base_memmap[VIRT_FLASH_OSPI].size;
	// DeviceState *gicdev = DEVICE(&s->mpu.gic);
	DeviceState *nor_dev = NULL, *nand_dev = NULL;
	BusState *spi_bus;
	qemu_irq cs_line;
	DeviceState *dev;
	MemoryRegion *mr;
	const int flash_num = 2;
	int j;
	char *name = g_strdup_printf("ospi0");

	object_initialize_child(OBJECT(s), name, &s->mpu.peri.ospi,
							TYPE_DESIGNWARE_SPI);
	dev = DEVICE(&s->mpu.peri.ospi);
	qdev_prop_set_uint32(dev, "num-cs", flash_num);
	qdev_prop_set_uint32(dev, "len-flash-dev", flash_num);
	for (j = 0; j < flash_num; j++) {
		char *propname = g_strdup_printf("flash-dev[%d]", j);
		if (j)
			nand_dev = create_nand_flash(s, j + 1);
		else
			nor_dev = create_nor_flash(s, j + 1);
		qdev_prop_set_uint64(dev, propname, (uint64_t)(j ? nand_dev : nor_dev));
		g_free(propname);
	}

	sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

	mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
	memory_region_add_subregion(sysmem, base, mr);

	spi_bus = BUS(s->mpu.peri.ospi.spi);
	g_free(name);

	/* nor flash memory */
	qdev_realize_and_unref(nor_dev, spi_bus, &error_fatal);
	cs_line = qdev_get_gpio_in_named(nor_dev, SSI_GPIO_CS, 0);
	sysbus_connect_irq(SYS_BUS_DEVICE(&s->mpu.peri.ospi), 1, cs_line);

	/* nand flash memory */
	qdev_realize_and_unref(nand_dev, spi_bus, &error_fatal);
	cs_line = qdev_get_gpio_in_named(nand_dev, SSI_GPIO_CS, 0);
	sysbus_connect_irq(SYS_BUS_DEVICE(&s->mpu.peri.ospi), 2, cs_line);
}

static void create_emmc(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	// int irq = apu_irqmap[VIRT_FLASH_EMMC];
	hwaddr base = base_memmap[VIRT_FLASH_EMMC].base;
	hwaddr size = base_memmap[VIRT_FLASH_EMMC].size;
	// DeviceState *gicdev = DEVICE(&s->apu.gic);
	int i;

	for (i = 0; i < ARRAY_SIZE(s->mpu.peri.mmc); i++) {
		char *name = g_strdup_printf("sdhci%d", i);
		DeviceState *dev;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), name, &s->mpu.peri.mmc[i],
								TYPE_SYSBUS_SDHCI);
		dev = DEVICE(&s->mpu.peri.mmc[i]);
		object_property_set_uint(OBJECT(dev), "sd-spec-version", 3, &error_fatal);
		object_property_set_uint(OBJECT(dev), "capareg", 0x70156ecc02UL, &error_fatal);
		sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

		mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
		memory_region_add_subregion(sysmem, base, mr);

		// sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(gicdev, irq));

		base += size;
		// irq += 1;
		g_free(name);
	}
}

static void create_emmc_card(LagunaSafety *s, SDHCIState *mmc, int index)
{
	DriveInfo *di = drive_get(IF_EMMC, 0, index);
	BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;
	DeviceState *emmc;

	emmc = qdev_new(TYPE_EMMC);
	emmc->id = g_strdup_printf("emmc%d", index);
	object_property_add_child(OBJECT(mmc), "emmc[*]", OBJECT(emmc));
	object_property_set_uint(OBJECT(emmc), "spec_version", 3, &error_fatal);
	object_property_set_uint(OBJECT(emmc), "boot-config", 0x0, &error_fatal);
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

static void create_memmap(LagunaSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	hwaddr ocm_base = base_memmap[VIRT_OCM].base;
	hwaddr ocm_size = base_memmap[VIRT_OCM].size;
	hwaddr iram_base = base_memmap[VIRT_IRAM].base;
	hwaddr iram_size = base_memmap[VIRT_IRAM].size;

	memory_region_init_ram(&s->mr_ocm, OBJECT(s), "ocm", ocm_size, &error_fatal);
	memory_region_add_subregion(sysmem, ocm_base, &s->mr_ocm);

	memory_region_init_ram(&s->mr_iram, OBJECT(s), "iram", iram_size, &error_fatal);
	memory_region_add_subregion(sysmem, iram_base, &s->mr_iram);
}

static void create_unimp(LagunaSafety *s)
{
	int i;
	char *name;

	for (i = 0; i < ARRAY_SIZE(unimp_memmap); i++) {
		name = g_strdup_printf("unimp_device@%08lx", unimp_memmap[i].base);
		create_unimplemented_device(name, unimp_memmap[i].base, unimp_memmap[i].size);
		g_free(name);
	}
}

static void lua_safety_realize(DeviceState *dev, Error **errp)
{
	LagunaSafety *s = LUA_SAFETY(dev);
	int i;

	if (s->cfg.lockstep) {
		create_apu_lockstep(s);
		create_tcm_slave_lockstep(s);
	} else {
		create_apu_split(s);
		create_tcm_slave_split(s);
	}
	create_gic(s);
	create_uart(s);
	create_timer(s);
	create_ethernet(s);
	create_qspi_nor_flash(s);
	create_ospi_nor_nand_flash(s);
	create_emmc(s);
	create_memmap(s);
	create_unimp(s);

	for (i = 0; i < ARRAY_SIZE(s->mpu.peri.mmc); i++) {
		if (i == 0) {
			create_emmc_card(s, &s->mpu.peri.mmc[i], i);
			continue;
		}
		create_sd_card(&s->mpu.peri.mmc[i], i);
	}
}

static Property lua_safety_properties[] = {
	DEFINE_PROP_BOOL("lockstep", LagunaSafety, cfg.lockstep, true),
	DEFINE_PROP_STRING("nor-flash", LagunaSafety, cfg.nor_flash),
	DEFINE_PROP_END_OF_LIST()
};

static void lua_safety_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	dc->realize = lua_safety_realize;
	device_class_set_props(dc, lua_safety_properties);
}

static void lua_safety_init(Object *obj)
{
}

static const TypeInfo lua_safety_info = {
	.name = TYPE_LUA_SAFETY,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LagunaSafety),
	.instance_init = lua_safety_init,
	.class_init = lua_safety_class_init,
};

static void lua_safety_register_types(void)
{
	type_register_static(&lua_safety_info);
}

type_init(lua_safety_register_types);