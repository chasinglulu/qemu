/*
 * Laguna Safety Island Virtual Platform emulation
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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/arm/fdt.h"
#include "cpu.h"
#include "hw/qdev-properties.h"
#include "hw/arm/laguna-safety.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qapi/visitor.h"

#define TYPE_LUA_SAFETY_VIRT_MACHINE MACHINE_TYPE_NAME("lua-safety-virt")
OBJECT_DECLARE_SIMPLE_TYPE(LuaSafetyVirt, LUA_SAFETY_VIRT_MACHINE)

struct LuaSafetyVirt {
	MachineState parent_obj;
	Notifier machine_done;

	LagunaSafety safety;

	void *fdt;
	int fdt_size;
	uint32_t clock_phandle;
	uint32_t gic_phandle;
	uint32_t msi_phandle;
	int psci_conduit;
	struct arm_boot_info bootinfo;
};

static void create_fdt(LuaSafetyVirt *s)
{
	MachineClass *mc = MACHINE_GET_CLASS(s);

	s->fdt = create_device_tree(&s->fdt_size);
	if (!s->fdt) {
		error_report("create_device_tree() failed");
		exit(1);
	}

	/* Allocate all phandles.  */
	s->gic_phandle = qemu_fdt_alloc_phandle(s->fdt);
	s->clock_phandle = qemu_fdt_alloc_phandle(s->fdt);

	/* Create /chosen node for load_dtb.  */
	qemu_fdt_add_subnode(s->fdt, "/chosen");

	/* Create /soc node for load_dtb. */
	qemu_fdt_add_subnode(s->fdt, "/soc");
	qemu_fdt_setprop(s->fdt, "/soc", "ranges", NULL, 0);
	qemu_fdt_setprop_cell(s->fdt, "/soc", "#size-cells", 0x1);
	qemu_fdt_setprop_cell(s->fdt, "/soc", "#address-cells", 0x1);
	qemu_fdt_setprop_string(s->fdt, "/soc", "compatible", "simple-bus");

	/* Header */
	qemu_fdt_setprop_cell(s->fdt, "/", "interrupt-parent", s->gic_phandle);
	qemu_fdt_setprop_cell(s->fdt, "/", "#size-cells", 0x1);
	qemu_fdt_setprop_cell(s->fdt, "/", "#address-cells", 0x1);
	qemu_fdt_setprop_string(s->fdt, "/", "model", mc->desc);
	qemu_fdt_setprop_string(s->fdt, "/", "compatible", "lua-safety-virt");
}

static void fdt_add_clk_nodes(LuaSafetyVirt *vms)
{
	/* Clock node, for the benefit of the UART. The kernel device tree
	 * binding documentation claims the uart node clock properties are
	 * optional.
	 */
	vms->clock_phandle = qemu_fdt_alloc_phandle(vms->fdt);
	qemu_fdt_add_subnode(vms->fdt, "/apb-pclk");
	qemu_fdt_setprop_string(vms->fdt, "/apb-pclk", "compatible", "fixed-clock");
	qemu_fdt_setprop_cell(vms->fdt, "/apb-pclk", "#clock-cells", 0x0);
	qemu_fdt_setprop_cell(vms->fdt, "/apb-pclk", "clock-frequency", 24000000);
	qemu_fdt_setprop_string(vms->fdt, "/apb-pclk", "clock-output-names",
								"clk24mhz");
	qemu_fdt_setprop_cell(vms->fdt, "/apb-pclk", "phandle", vms->clock_phandle);
	}

static void fdt_add_timer_nodes(const LuaSafetyVirt *vms)
{
	// uint32_t irqflags = GIC_FDT_IRQ_FLAGS_LEVEL_HI;
	// const char compat[] = "arm,armv8-timer";

	// qemu_fdt_add_subnode(vms->fdt, "/timer");
	// qemu_fdt_setprop(vms->fdt, "/timer", "compatible",
	// 					compat, sizeof(compat));

	// qemu_fdt_setprop(vms->fdt, "/timer", "always-on", NULL, 0);
	// qemu_fdt_setprop_cells(vms->fdt, "/timer", "interrupts",
	// 					GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_S_EL1_IRQ, irqflags,
	// 					GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_NS_EL1_IRQ, irqflags,
	// 					GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_VIRT_IRQ, irqflags,
	// 					GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_NS_EL2_IRQ, irqflags);
}

static void fdt_add_cpu_nodes(const LuaSafetyVirt *vms)
{
	int cpu;
	int addr_cells = 1;
	MachineState *ms = MACHINE(vms);
	int smp_cpus = ms->smp.cpus;

	/*
	 * See Linux Documentation/devicetree/bindings/arm/cpus.yaml
	 * On ARM v8 64-bit systems value should be set to 2,
	 * that corresponds to the MPIDR_EL1 register size.
	 * If MPIDR_EL1[63:32] value is equal to 0 on all CPUs
	 * in the system, #address-cells can be set to 1, since
	 * MPIDR_EL1[63:32] bits are not used for CPUs
	 * identification.
	 *
	 * Here we actually don't know whether our system is 32- or 64-bit one.
	 * The simplest way to go is to examine affinity IDs of all our CPUs. If
	 * at least one of them has Aff3 populated, we set #address-cells to 2.
	 */
	for (cpu = 0; cpu < smp_cpus; cpu++) {
		ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(cpu));

		if (armcpu->mp_affinity & ARM_AFF3_MASK) {
			addr_cells = 2;
			break;
		}
	}

	qemu_fdt_add_subnode(vms->fdt, "/cpus");

	qemu_fdt_setprop_cell(vms->fdt, "/cpus", "#address-cells", addr_cells);
	qemu_fdt_setprop_cell(vms->fdt, "/cpus", "#size-cells", 0x0);

	for (cpu = smp_cpus - 1; cpu >= 0; cpu--) {
		char *nodename = g_strdup_printf("/cpus/cpu@%x", cpu);
		ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(cpu));

		qemu_fdt_add_subnode(vms->fdt, nodename);
		qemu_fdt_setprop_string(vms->fdt, nodename, "device_type", "cpu");
		qemu_fdt_setprop_string(vms->fdt, nodename, "compatible",
									armcpu->dtb_compatible);

		if (addr_cells == 2) {
			qemu_fdt_setprop_u64(vms->fdt, nodename, "reg",
									armcpu->mp_affinity);
		} else {
			qemu_fdt_setprop_cell(vms->fdt, nodename, "reg",
									armcpu->mp_affinity);
		}

		qemu_fdt_setprop_cell(vms->fdt, nodename, "phandle",
									qemu_fdt_alloc_phandle(vms->fdt));

		g_free(nodename);
	}
}

static void fdt_add_gic_node(LuaSafetyVirt *vms)
{
	char *nodename;

	vms->gic_phandle = qemu_fdt_alloc_phandle(vms->fdt);
	qemu_fdt_setprop_cell(vms->fdt, "/", "interrupt-parent", vms->gic_phandle);

	nodename = g_strdup_printf("/gic@%" PRIx64,
								base_memmap[VIRT_GIC_DIST].base);
	qemu_fdt_add_subnode(vms->fdt, nodename);
	qemu_fdt_setprop_cell(vms->fdt, nodename, "#interrupt-cells", 3);
	qemu_fdt_setprop(vms->fdt, nodename, "interrupt-controller", NULL, 0);
	qemu_fdt_setprop_cell(vms->fdt, nodename, "#address-cells", 0x1);
	qemu_fdt_setprop_cell(vms->fdt, nodename, "#size-cells", 0x1);
	qemu_fdt_setprop(vms->fdt, nodename, "ranges", NULL, 0);
	qemu_fdt_setprop_string(vms->fdt, nodename, "compatible",
							"arm,gic");
	qemu_fdt_setprop_cell(vms->fdt, nodename,
							"#redistributor-regions", 1);
	qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
									1, base_memmap[VIRT_GIC_DIST].base,
									1, base_memmap[VIRT_GIC_DIST].size,
									1, base_memmap[VIRT_GIC_CPU].base,
									1, base_memmap[VIRT_GIC_CPU].size);

	qemu_fdt_setprop_cell(vms->fdt, nodename, "phandle", vms->gic_phandle);
	g_free(nodename);
}

static void fdt_add_aliases_nodes(LuaSafetyVirt *vms)
{
	int i;
	hwaddr base = base_memmap[VIRT_UART].base;
	hwaddr size = base_memmap[VIRT_UART].size;
	qemu_fdt_add_subnode(vms->fdt, "/aliases");
	char *nodename, *propname;

	for (i = 0; i < ARRAY_SIZE(vms->safety.mpu.peri.uarts); i++) {
		nodename = g_strdup_printf("/serial@%" PRIx64, base);
		propname = g_strdup_printf("serial%d", i);
		qemu_fdt_setprop_string(vms->fdt, "/aliases", propname, nodename);

		base += size;

		g_free(nodename);
		g_free(propname);
	}
}

static void fdt_add_uart_nodes(const LuaSafetyVirt *vms)
{
	char *nodename;
	uint32_t nr_uart = ARRAY_SIZE(vms->safety.mpu.peri.uarts);
	hwaddr base = base_memmap[VIRT_UART].base;
	hwaddr size = base_memmap[VIRT_UART].size;
	int irq = mpu_irqmap[VIRT_UART];
	const char compat[] = "ns16550";
	const char clocknames[] = "apb_pclk";
	int i;

	/* Create nodes in incremental address */
	base = base + size * (nr_uart - 1);
	irq = irq + nr_uart - 1;
	for (i = nr_uart - 1; i >= 0; i--) {
		nodename = g_strdup_printf("/soc/serial@%" PRIx64, base);
		qemu_fdt_add_subnode(vms->fdt, nodename);
		/* Note that we can't use setprop_string because of the embedded NUL */
		qemu_fdt_setprop(vms->fdt, nodename, "compatible",
							compat, sizeof(compat));
		qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
										1, base, 1, size);
		qemu_fdt_setprop_cells(vms->fdt, nodename, "interrupts",
								GIC_FDT_IRQ_TYPE_SPI, irq,
								GIC_FDT_IRQ_FLAGS_LEVEL_HI);
		qemu_fdt_setprop_cell(vms->fdt, nodename, "current-speed", 115200);
		qemu_fdt_setprop_cell(vms->fdt, nodename, "clock-frequency", 24000000);
		qemu_fdt_setprop_cell(vms->fdt, nodename, "reg-io-width", 4);
		qemu_fdt_setprop_cell(vms->fdt, nodename, "reg-shift", 2);
		qemu_fdt_setprop_cell(vms->fdt, nodename, "clocks",
									vms->clock_phandle);
		qemu_fdt_setprop(vms->fdt, nodename, "clock-names",
								clocknames, sizeof(clocknames));
		qemu_fdt_setprop(vms->fdt, nodename, "u-boot,dm-pre-reloc", NULL, 0);
		qemu_fdt_setprop(vms->fdt, nodename, "u-boot,dm-spl", NULL, 0);
		base -= size;
		irq -= 1;
		if (i == 0) {
			/* Select UART0 as console  */
			qemu_fdt_setprop_string(vms->fdt, "/chosen", "stdout-path", nodename);
		}

		g_free(nodename);
	}
}

static void *lua_virt_dtb(const struct arm_boot_info *binfo, int *fdt_size)
{
	const LuaSafetyVirt *board = container_of(binfo, LuaSafetyVirt, bootinfo);

	*fdt_size = board->fdt_size;
	return board->fdt;
}

static void fdt_nop_memory_nodes(void *fdt, Error **errp)
{
	Error *err = NULL;
	char **node_path;
	int n = 0;

	node_path = qemu_fdt_node_unit_path(fdt, "memory", &err);
	if (err) {
		error_propagate(errp, err);
		return;
	}
	while (node_path[n]) {
		if (g_str_has_prefix(node_path[n], "/memory")) {
			qemu_fdt_nop_node(fdt, node_path[n]);
		}
		n++;
	}
	g_strfreev(node_path);
}

static void fdt_add_memory_nodes(LuaSafetyVirt *s, void *fdt, uint64_t ram_size)
{
	/* Describes the various split DDR access regions.  */
	static struct {
		uint64_t base;
		uint64_t size;
	} addr_ranges[2];
	uint64_t mem_reg_prop[4] = {0};
	uint64_t size = ram_size;
	uint64_t mapsize;
	Error *err = NULL;
	char *name;
	int i;

	addr_ranges[0].base = base_memmap[VIRT_OCM].base;
	addr_ranges[0].size = base_memmap[VIRT_OCM].size;

	fdt_nop_memory_nodes(fdt, &err);
	if (err) {
		error_report_err(err);
		return;
	}

	name = g_strdup_printf("/memory@%lx", base_memmap[VIRT_OCM].base);

	mapsize = size < addr_ranges[0].size ? size : addr_ranges[0].size;

	mem_reg_prop[0] = addr_ranges[0].base;
	mem_reg_prop[1] = mapsize;
	size -= mapsize;
	i = ARRAY_SIZE(addr_ranges);

	qemu_fdt_add_subnode(fdt, name);
	qemu_fdt_setprop_string(fdt, name, "device_type", "memory");

	switch (i) {
	case 1:
		qemu_fdt_setprop_sized_cells(fdt, name, "reg",
										1, mem_reg_prop[0],
										1, mem_reg_prop[1]);
		break;
	case 2:
		qemu_fdt_setprop_sized_cells(fdt, name, "reg",
										1, mem_reg_prop[0],
										1, mem_reg_prop[1],
										1, mem_reg_prop[2],
										1, mem_reg_prop[3]);
		break;
	default:
		g_assert_not_reached();
	}
	g_free(name);
}

static void lua_virt_modify_dtb(const struct arm_boot_info *binfo, void *fdt)
{
	LuaSafetyVirt *s = container_of(binfo, LuaSafetyVirt, bootinfo);

	fdt_add_memory_nodes(s, fdt, binfo->ram_size);
}

static void lua_virt_mach_done(Notifier *notifier, void *data)
{
	LuaSafetyVirt *vms = container_of(notifier, LuaSafetyVirt,
											machine_done);
	MachineState *ms = MACHINE(vms);
	ARMCPU *cpu = ARM_CPU(first_cpu);
	struct arm_boot_info *info = &vms->bootinfo;
	AddressSpace *as = arm_boot_address_space(cpu, info);

	if (arm_load_dtb(info->dtb_start, info, info->dtb_limit, as, ms) < 0)
		exit(1);
}

static void lua_virt_mach_init(MachineState *machine)
{
	LuaSafetyVirt *vms = LUA_SAFETY_VIRT_MACHINE(machine);

	object_initialize_child(OBJECT(machine), "lua-safety", &vms->safety,
							TYPE_LUA_SAFETY);

	sysbus_realize_and_unref(SYS_BUS_DEVICE(&vms->safety), &error_fatal);

	create_fdt(vms);
	fdt_add_clk_nodes(vms);
	fdt_add_cpu_nodes(vms);
	fdt_add_gic_node(vms);
	fdt_add_timer_nodes(vms);
	fdt_add_uart_nodes(vms);
	fdt_add_aliases_nodes(vms);

	vms->bootinfo.ram_size = machine->ram_size;
	vms->bootinfo.board_id = -1;
	vms->bootinfo.loader_start = base_memmap[VIRT_OCM].base;
	vms->bootinfo.get_dtb = lua_virt_dtb;
	vms->bootinfo.modify_dtb = lua_virt_modify_dtb;
	vms->bootinfo.skip_dtb_autoload = true;
	vms->bootinfo.psci_conduit = vms->psci_conduit;
	arm_load_kernel(ARM_CPU(first_cpu), machine, &vms->bootinfo);

	vms->machine_done.notify = lua_virt_mach_done;
	qemu_add_machine_init_done_notifier(&vms->machine_done);
}

static void lua_virt_mach_instance_init(Object *obj)
{
}

static void lua_virt_mach_class_init(ObjectClass *oc, void *data)
{
	MachineClass *mc = MACHINE_CLASS(oc);

	mc->desc = "Laguna Safety Island Virtual Platform";
	mc->init = lua_virt_mach_init;
	mc->min_cpus = LUA_SAFETY_NR_MCPUS;
	mc->max_cpus = LUA_SAFETY_NR_MCPUS;
	mc->default_cpus = LUA_SAFETY_NR_MCPUS;
	mc->default_cpu_type = LUA_SAFETY_MCPU_TYPE;
	mc->no_cdrom = 1;
	mc->no_sdcard = 1;
	mc->no_floppy = 1;
	mc->block_default_type = IF_NONE;
}

static const TypeInfo lua_virt_mach_info = {
	.name           = TYPE_LUA_SAFETY_VIRT_MACHINE,
	.parent         = TYPE_MACHINE,
	.class_init     = lua_virt_mach_class_init,
	.instance_init  = lua_virt_mach_instance_init,
	.instance_size  = sizeof(LuaSafetyVirt),
};

static void lua_virt_machine_init(void)
{
	type_register_static(&lua_virt_mach_info);
}

type_init(lua_virt_machine_init)
