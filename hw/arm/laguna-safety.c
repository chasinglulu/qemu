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

static void create_apu(LagunaSafety *s)
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

		name = g_strdup_printf("tcm%x", i);
		memory_region_init_ram(&s->mr_tcm[i], OBJECT(s), name, tcm_size, &error_fatal);
		memory_region_add_subregion(&s->mr_cpu[i], tcm_base, &s->mr_tcm[i]);
		g_free(name);

		name = g_strdup_printf("cpu%d-alias", i);
		memory_region_init_alias(&s->mr_cpu_alias[i], OBJECT(s), name, sysmem, tcm_size, 0x100000000);
		g_free(name);

		memory_region_add_subregion_overlap(&s->mr_cpu[i], tcm_size, &s->mr_cpu_alias[i], 0);

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
	sysbus_mmio_map(gicbusdev, 0, base_memmap[VIRT_GIC_DIST].base);
	sysbus_mmio_map(gicbusdev, 1, base_memmap[VIRT_GIC_CPU].base);

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
								TYPE_DW_UART);
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

static void lua_safety_realize(DeviceState *dev, Error **errp)
{
	LagunaSafety *s = LUA_SAFETY(dev);

	create_apu(s);
	create_gic(s);
	create_uart(s);
	create_timer(s);
	create_memmap(s);
	// create_unimp(s);
}

static Property lua_safety_properties[] = {
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