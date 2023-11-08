/*
 * Lambert safety island emulation
 *
 * Copyright (C) 2023 Charleye <wangkart@aliyun.com>
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
#include "hw/riscv/lmt-safety.h"
#include "hw/misc/unimp.h"
#include "sysemu/hostmem.h"
#include "migration/vmstate.h"

static const char *valid_cpus[] = {
	RISCV_CPU_TYPE_NAME("rv32"),
	RISCV_CPU_TYPE_NAME("thead-e907"),
};

static bool cpu_type_valid(const char *cpu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(valid_cpus); i++) {
		if (strcmp(cpu, valid_cpus[i]) == 0) {
			return true;
		}
	}
	return false;
}

static void create_riscv(LambertSafety *s)
{
	Object *cpusobj;

	if (!cpu_type_valid(s->cfg.cpu_type)) {
		error_report("lmt-safety: CPU type %s not supported", s->cfg.cpu_type);
		exit(1);
	}

	object_initialize_child(OBJECT(s), "riscvs", &s->safety.cpus, TYPE_RISCV_HART_ARRAY);
	cpusobj = OBJECT(&s->safety.cpus);

	object_property_set_str(cpusobj, "cpu-type", s->cfg.cpu_type, &error_abort);
	object_property_set_int(cpusobj, "num-harts", s->cfg.num_harts, &error_abort);

	sysbus_realize(SYS_BUS_DEVICE(cpusobj), &error_fatal);
}

static void create_clic(LambertSafety *s)
{
	hwaddr base = base_memmap[VIRT_CLIC].base;
	DeviceState *dev;

	object_initialize_child(OBJECT(s), "clic", &s->safety.clic, TYPE_RISCV_CLIC);
	dev = DEVICE(&s->safety.clic);

	qdev_prop_set_bit(dev, "prv-s", false);
	qdev_prop_set_bit(dev, "prv-u", false);
	qdev_prop_set_uint32(dev, "num-harts", s->cfg.num_harts);
	qdev_prop_set_uint32(dev, "num-sources", LMT_SAFETY_IRQS_NUM);
	qdev_prop_set_uint32(dev, "clicintctlbits", 3);
	qdev_prop_set_uint64(dev, "mclicbase", base);
	qdev_prop_set_string(dev, "version", "0.8");

	sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
	sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
}

static void create_aclint(LambertSafety *s)
{
	hwaddr base = base_memmap[VIRT_CLINT].base;

	riscv_aclint_swi_create(base, 0, s->cfg.num_harts, false);
	riscv_aclint_mtimer_create(base + RISCV_ACLINT_SWI_SIZE,
		RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
		0, s->cfg.num_harts,
		RISCV_ACLINT_DEFAULT_MTIMECMP,
		RISCV_ACLINT_DEFAULT_MTIME,
		RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
}

static void create_uart(LambertSafety *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = irqmap[VIRT_UART];
	hwaddr base = base_memmap[VIRT_UART].base;
	hwaddr size = base_memmap[VIRT_UART].size;
	DeviceState *clicdev = DEVICE(&s->safety.clic);
	int i;

	for (i = 0; i < ARRAY_SIZE(s->safety.peri.uarts); i++) {
		char *name = g_strdup_printf("uart%d", i);
		DeviceState *dev;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), name, &s->safety.peri.uarts[i],
								TYPE_DW_UART);
		dev = DEVICE(&s->safety.peri.uarts[i]);
		qdev_prop_set_uint8(dev, "regshift", 2);
		qdev_prop_set_uint32(dev, "baudbase", 115200);
		qdev_prop_set_uint8(dev, "endianness", DEVICE_LITTLE_ENDIAN);
		qdev_prop_set_chr(dev, "chardev", serial_hd(i));
		qdev_prop_set_uint8(dev, "index", i);
		sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

		mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
		memory_region_add_subregion(sysmem, base, mr);

		sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(clicdev, irq));

		base += size;
		irq += 1;
		g_free(name);
	}
}

static void create_iram_memmap(LambertSafety *s)
{
	Object *backend;
	MemoryRegion *mr;
	ram_addr_t backend_size;
	hwaddr iram_base = base_memmap[VIRT_IRAM].base;
	hwaddr iram_size = base_memmap[VIRT_IRAM].size;

	if (s->cfg.memdev) {
		backend = object_resolve_path_type(s->cfg.memdev,
											TYPE_MEMORY_BACKEND, NULL);
		if (!backend) {
			error_report("Memory backend '%s' not found", s->cfg.memdev);
			exit(EXIT_FAILURE);
		}

		backend_size = object_property_get_uint(backend, "size",  &error_abort);
		if (backend_size != iram_size) {
			error_report("Safety Island IRAM memory size does not match the size of the memory backend");
			exit(EXIT_FAILURE);
		}

		mr = host_memory_backend_get_memory(MEMORY_BACKEND(backend));
		if (host_memory_backend_is_mapped(MEMORY_BACKEND(backend))) {
			error_report("memory backend %s can't be used multiple times.",
							object_get_canonical_path_component(OBJECT(backend)));
			exit(EXIT_FAILURE);
		}
		host_memory_backend_set_mapped(MEMORY_BACKEND(backend), true);
		vmstate_register_ram_global(mr);

		memory_region_init_alias(&s->mr_iram, OBJECT(s), "iram", mr, 0, iram_size);
	} else {
		memory_region_init_ram(&s->mr_iram, OBJECT(s), "iram", iram_size, &error_fatal);
	}

	memory_region_add_subregion(get_system_memory(), iram_base, &s->mr_iram);
}

static void create_memmap(LambertSafety *s)
{
	uint64_t cfg_ddr_size = memory_region_size(s->cfg.mr_ddr);
	MemoryRegion *sysmem = get_system_memory();
	hwaddr base = base_memmap[VIRT_MEM].base;
	hwaddr size = base_memmap[VIRT_MEM].size;
	uint64_t offset = 0;
	char *name;
	uint64_t mapsize;

	mapsize = cfg_ddr_size < size ? cfg_ddr_size : size;
	name = g_strdup_printf("lmt-safety-mem");
	/* Create the MR alias.  */
	memory_region_init_alias(&s->mr_mem, OBJECT(s),
								name, s->cfg.mr_ddr,
								offset, mapsize);

	/* Map it onto the main system MR.  */
	memory_region_add_subregion(sysmem, base, &s->mr_mem);
	g_free(name);

	/* Map iram_safety into the main system memory */
	create_iram_memmap(s);
}

static void create_unimp(LambertSafety *s)
{
}

static void lmt_safety_realize(DeviceState *dev, Error **errp)
{
	LambertSafety *s = LMT_SAFETY(dev);

	create_riscv(s);
	create_clic(s);
	create_aclint(s);
	create_uart(s);
	create_memmap(s);
	create_unimp(s);
}

static Property lmt_safety_properties[] = {
	DEFINE_PROP_LINK("lmt-safety.mem", LambertSafety, cfg.mr_ddr, TYPE_MEMORY_REGION,
						MemoryRegion*),
	DEFINE_PROP_STRING("memdev", LambertSafety, cfg.memdev),
	DEFINE_PROP_STRING("cpu-type", LambertSafety, cfg.cpu_type),
	DEFINE_PROP_UINT32("num-harts", LambertSafety, cfg.num_harts, 1),
	DEFINE_PROP_END_OF_LIST()
};

static void lmt_safety_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	dc->realize = lmt_safety_realize;
	device_class_set_props(dc, lmt_safety_properties);
}

static void lmt_safety_init(Object *obj)
{
}

static const TypeInfo lmt_safety_info = {
	.name = TYPE_LMT_SAFETY,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LambertSafety),
	.instance_init = lmt_safety_init,
	.class_init = lmt_safety_class_init,
};

static void lmt_safety_register_types(void)
{
	type_register_static(&lmt_safety_info);
}

type_init(lmt_safety_register_types);
