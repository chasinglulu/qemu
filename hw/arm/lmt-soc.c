/*
 * Lambert SoC emulation
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
#include "hw/arm/lmt-soc.h"
#include "hw/misc/unimp.h"
#include "sysemu/hostmem.h"
#include "migration/vmstate.h"

static bool lmt_soc_get_virt(Object *obj, Error **errp)
{
	LambertSoC *s = LMT_SOC(obj);

	return s->cfg.virt;
}

static void lmt_soc_set_virt(Object *obj, bool value, Error **errp)
{
	LambertSoC *s = LMT_SOC(obj);

	s->cfg.virt = value;
}

static bool lmt_soc_get_secure(Object *obj, Error **errp)
{
	LambertSoC *s = LMT_SOC(obj);

	return s->cfg.secure;
}

static void lmt_soc_set_secure(Object *obj, bool value, Error **errp)
{
	LambertSoC *s = LMT_SOC(obj);

	s->cfg.secure = value;
}

static const char *valid_cpus[] = {
	ARM_CPU_TYPE_NAME("cortex-a55"),
	ARM_CPU_TYPE_NAME("cortex-a76"),
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

static void create_apu(LambertSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int i;

	if (!cpu_type_valid(s->cfg.cpu_type)) {
		error_report("lmt-soc: CPU type %s not supported", s->cfg.cpu_type);
		exit(1);
	}

	for (i = 0; i < ARRAY_SIZE(s->apu.cpus); i++) {
		Object *cpuobj;

		object_initialize_child(OBJECT(s), "apu[*]", &s->apu.cpus[i],
								s->cfg.cpu_type);
		cpuobj = OBJECT(&s->apu.cpus[i]);
		if (i) {
			/* Secondary CPUs start in powered-down state */
			object_property_set_bool(cpuobj, "start-powered-off", true,
										&error_abort);
		}

		object_property_set_int(cpuobj, "mp-affinity",
								lmt_cpu_mp_affinity(i), NULL);

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

static void create_gic(LambertSoC *s)
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
							LMT_SOC_NUM_IRQS + 32);
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
		int ppibase = LMT_SOC_NUM_IRQS + i * GIC_INTERNAL + GIC_NR_SGIS;
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

static void create_uart(LambertSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = a76irqmap[VIRT_UART];
	hwaddr base = base_memmap[VIRT_UART].base;
	hwaddr size = base_memmap[VIRT_UART].size;
	DeviceState *gicdev = DEVICE(&s->apu.gic);
	int i;

	for (i = 0; i < ARRAY_SIZE(s->apu.peri.uarts); i++) {
		char *name = g_strdup_printf("uart%d", i);
		DeviceState *dev;
		MemoryRegion *mr;

		object_initialize_child(OBJECT(s), name, &s->apu.peri.uarts[i],
								TYPE_DW_UART);
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

static void create_ethernet(LambertSoC *s)
{
	MemoryRegion *sysmem = get_system_memory();
	int irq = a76irqmap[VIRT_EMAC];
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

static void create_riscv_iram_memmap(LambertSoC *s)
{
	Object *backend;
	MemoryRegion *mr;
	ram_addr_t backend_size;
	hwaddr iram_safety_base = base_memmap[VIRT_IRAM_SAFETY].base;
	hwaddr iram_safety_size = base_memmap[VIRT_IRAM_SAFETY].size;

	if (s->cfg.riscv_memdev) {
		printf("%s\n", s->cfg.riscv_memdev);
		backend = object_resolve_path_type(s->cfg.riscv_memdev,
											TYPE_MEMORY_BACKEND, NULL);
		if (!backend) {
			error_report("Memory backend '%s' not found", s->cfg.riscv_memdev);
			exit(EXIT_FAILURE);
		}

		backend_size = object_property_get_uint(backend, "size",  &error_abort);
		if (backend_size != iram_safety_size) {
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

		memory_region_init_alias(&s->mr_iram_safety, OBJECT(s), "iram_safety", mr, 0, iram_safety_size);
	} else {
		memory_region_init_ram(&s->mr_iram_safety, OBJECT(s), "iram_safety", iram_safety_size, &error_fatal);
	}

	memory_region_add_subregion(get_system_memory(), iram_safety_base, &s->mr_iram_safety);
}

/* This takes the board allocated linear DDR memory and creates aliases
 * for each split DDR range/aperture on the address map.
 */
static void create_ddr_memmap(LambertSoC *s)
{
	uint64_t cfg_ddr_size = memory_region_size(s->cfg.mr_ddr);
	MemoryRegion *sysmem = get_system_memory();
	hwaddr base = base_memmap[VIRT_MEM].base;
	hwaddr size = base_memmap[VIRT_MEM].size;
	hwaddr iram_base = base_memmap[VIRT_IRAM].base;
	hwaddr iram_size = base_memmap[VIRT_IRAM].size;
	uint64_t offset = 0;
	char *name;
	uint64_t mapsize;

	mapsize = cfg_ddr_size < size ? cfg_ddr_size : size;
	name = g_strdup_printf("lmt-ddr");
	/* Create the MR alias.  */
	memory_region_init_alias(&s->mr_ddr, OBJECT(s),
								name, s->cfg.mr_ddr,
								offset, mapsize);

	/* Map it onto the main system MR.  */
	memory_region_add_subregion(sysmem, base, &s->mr_ddr);
	g_free(name);

	memory_region_init_ram(&s->mr_iram, OBJECT(s), "iram", iram_size, &error_fatal);
	memory_region_add_subregion(sysmem, iram_base, &s->mr_iram);

	/* Map iram_safety into the main system memory */
	create_riscv_iram_memmap(s);
}

static void create_unimp(LambertSoC *s)
{
}

static void lmt_soc_realize(DeviceState *dev, Error **errp)
{
	LambertSoC *s = LMT_SOC(dev);

	create_apu(s);
	create_gic(s);
	create_uart(s);
	create_ethernet(s);
	create_ddr_memmap(s);
	create_unimp(s);
}

static Property lmt_soc_properties[] = {
	DEFINE_PROP_LINK("lmt-soc.ddr", LambertSoC, cfg.mr_ddr, TYPE_MEMORY_REGION,
						MemoryRegion*),
	DEFINE_PROP_BOOL("has-emmc", LambertSoC, cfg.has_emmc, false),
	DEFINE_PROP_STRING("cpu-type", LambertSoC, cfg.cpu_type),
	DEFINE_PROP_STRING("riscv-memdev", LambertSoC, cfg.riscv_memdev),
	DEFINE_PROP_END_OF_LIST()
};

static void lmt_soc_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	dc->realize = lmt_soc_realize;
	device_class_set_props(dc, lmt_soc_properties);

	object_class_property_add_bool(klass, "virtualization", lmt_soc_get_virt,
									lmt_soc_set_virt);
	object_class_property_set_description(klass, "virtualization",
											"Set on/off to enable/disable emulating a "
											"guest CPU which implements the ARM "
											"Virtualization Extensions");
	object_class_property_add_bool(klass, "secure", lmt_soc_get_secure,
									lmt_soc_set_secure);
	object_class_property_set_description(klass, "secure",
											"Set on/off to enable/disable the ARM "
											"Security Extensions (TrustZone)");
}

static void lmt_soc_init(Object *obj)
{
}

static const TypeInfo lmt_soc_info = {
	.name = TYPE_LMT_SOC,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LambertSoC),
	.instance_init = lmt_soc_init,
	.class_init = lmt_soc_class_init,
};

static void lmt_soc_register_types(void)
{
	type_register_static(&lmt_soc_info);
}

type_init(lmt_soc_register_types);
