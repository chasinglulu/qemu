/*
 * Laguna Virtual Platform emulation
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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/arm/fdt.h"
#include "cpu.h"
#include "hw/qdev-properties.h"
#include "hw/arm/laguna.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qapi/visitor.h"

#define TYPE_LAGUNA_VIRT_MACHINE MACHINE_TYPE_NAME("lua-virt")
OBJECT_DECLARE_SIMPLE_TYPE(LagunaVirt, LAGUNA_VIRT_MACHINE)

struct LagunaVirt {
	MachineState parent_obj;
	Notifier machine_done;

	LagunaSoC lua;

	void *fdt;
	int fdt_size;
	uint32_t clock_phandle;
	uint32_t gic_phandle;
	uint32_t msi_phandle;
	int psci_conduit;
	struct arm_boot_info bootinfo;

	struct {
		bool virt;
		bool secure;
		bool has_emmc;
		uint8_t part_config;
		uint8_t bootmode;
		uint32_t bootstrap;
		const char *nor_flash;
		const char *nand;
		bool download;
		bool match;
	} cfg;
};

static void lua_virt_set_nor_flash(Object *obj, const char *str, Error **errp)
{
	LagunaVirt *s = LAGUNA_VIRT_MACHINE(obj);

	s->cfg.nor_flash = g_strdup(str);
}

static void lua_virt_set_emmc(Object *obj, bool value, Error **errp)
{
	LagunaVirt *s = LAGUNA_VIRT_MACHINE(obj);

	s->cfg.has_emmc = value;
}

static void lua_virt_set_download(Object *obj, bool value, Error **errp)
{
	LagunaVirt *s = LAGUNA_VIRT_MACHINE(obj);

	s->cfg.download = value;
}

static void lua_virt_set_match(Object *obj, bool value, Error **errp)
{
	LagunaVirt *s = LAGUNA_VIRT_MACHINE(obj);

	s->cfg.match = value;
}

static void lua_virt_set_bootstrap(Object *obj, Visitor *v,
						const char *name, void *opaque,
						Error **errp)
{
	LagunaVirt *s = LAGUNA_VIRT_MACHINE(obj);
	Error *error = NULL;
	uint32_t value;

	visit_type_uint32(v, name, &value, &error);
	if (error) {
		error_propagate(errp, error);
		return;
	}

	s->cfg.bootstrap = value;
}

static void lua_virt_set_part_config(Object *obj, Visitor *v,
						const char *name, void *opaque,
						Error **errp)
{
	LagunaVirt *s = LAGUNA_VIRT_MACHINE(obj);
	Error *error = NULL;
	uint8_t value;

	visit_type_uint8(v, name, &value, &error);
	if (error) {
		error_propagate(errp, error);
		return;
	}

	s->cfg.part_config = value;
}

static void lua_virt_set_bootmode(Object *obj, Visitor *v,
						const char *name, void *opaque,
						Error **errp)
{
	LagunaVirt *s = LAGUNA_VIRT_MACHINE(obj);
	Error *error = NULL;
	uint8_t value;

	visit_type_uint8(v, name, &value, &error);
	if (error) {
		error_propagate(errp, error);
		return;
	}

	s->cfg.bootmode = value;
}

static void lua_virt_set_virt(Object *obj, bool value, Error **errp)
{
	LagunaVirt *s = LAGUNA_VIRT_MACHINE(obj);

	s->cfg.virt = value;
}

static void lua_virt_set_secure(Object *obj, bool value, Error **errp)
{
	LagunaVirt *s = LAGUNA_VIRT_MACHINE(obj);

	s->cfg.secure = value;
}

static const CPUArchIdList *lua_virt_possible_cpu_arch_ids(MachineState *ms)
{
	int n;
	unsigned int max_cpus = ms->smp.max_cpus;
	LagunaVirt *vms = LAGUNA_VIRT_MACHINE(ms);
	MachineClass *mc = MACHINE_GET_CLASS(vms);

	if (ms->possible_cpus) {
		assert(ms->possible_cpus->len == max_cpus);
		return ms->possible_cpus;
	}

	ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
									sizeof(CPUArchId) * max_cpus);
	ms->possible_cpus->len = max_cpus;
	for (n = 0; n < ms->possible_cpus->len; n++) {
		ms->possible_cpus->cpus[n].type = ms->cpu_type;
		ms->possible_cpus->cpus[n].arch_id =
			lua_cpu_mp_affinity(n);

		assert(!mc->smp_props.dies_supported);
		ms->possible_cpus->cpus[n].props.has_socket_id = true;
		ms->possible_cpus->cpus[n].props.socket_id =
			n / (ms->smp.clusters * ms->smp.cores * ms->smp.threads);
		ms->possible_cpus->cpus[n].props.has_cluster_id = true;
		ms->possible_cpus->cpus[n].props.cluster_id =
			(n / (ms->smp.cores * ms->smp.threads)) % ms->smp.clusters;
		ms->possible_cpus->cpus[n].props.has_core_id = true;
		ms->possible_cpus->cpus[n].props.core_id =
			(n / ms->smp.threads) % ms->smp.cores;
		ms->possible_cpus->cpus[n].props.has_thread_id = true;
		ms->possible_cpus->cpus[n].props.thread_id =
			n % ms->smp.threads;
	}
	return ms->possible_cpus;
}

static void create_fdt(LagunaVirt *s)
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
	qemu_fdt_setprop_cell(s->fdt, "/soc", "#size-cells", 0x2);
	qemu_fdt_setprop_cell(s->fdt, "/soc", "#address-cells", 0x2);
	qemu_fdt_setprop_string(s->fdt, "/soc", "compatible", "simple-bus");

	/* Header */
	qemu_fdt_setprop_cell(s->fdt, "/", "interrupt-parent", s->gic_phandle);
	qemu_fdt_setprop_cell(s->fdt, "/", "#size-cells", 0x2);
	qemu_fdt_setprop_cell(s->fdt, "/", "#address-cells", 0x2);
	qemu_fdt_setprop_string(s->fdt, "/", "model", mc->desc);
	qemu_fdt_setprop_string(s->fdt, "/", "compatible", "laguna-virt");
}

static void fdt_add_clk_nodes(LagunaVirt *vms)
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

static void fdt_add_timer_nodes(const LagunaVirt *vms)
{
	uint32_t irqflags = GIC_FDT_IRQ_FLAGS_LEVEL_HI;
	const char compat[] = "arm,armv8-timer";

	qemu_fdt_add_subnode(vms->fdt, "/timer");
	qemu_fdt_setprop(vms->fdt, "/timer", "compatible",
						compat, sizeof(compat));

	qemu_fdt_setprop(vms->fdt, "/timer", "always-on", NULL, 0);
	qemu_fdt_setprop_cells(vms->fdt, "/timer", "interrupts",
						GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_S_EL1_IRQ, irqflags,
						GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_NS_EL1_IRQ, irqflags,
						GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_VIRT_IRQ, irqflags,
						GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_NS_EL2_IRQ, irqflags);
}

static void fdt_add_cpu_nodes(const LagunaVirt *vms)
{
	int cpu;
	int addr_cells = 1;
	MachineState *ms = MACHINE(vms);
	MachineClass *mc = MACHINE_GET_CLASS(ms);
	int smp_cpus = ms->smp.cpus;

	mc->possible_cpu_arch_ids(ms);
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
		char *nodename = g_strdup_printf("/cpus/cpu@%lx", ms->possible_cpus->cpus[cpu].arch_id);
		ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(cpu));
		CPUState *cs = CPU(armcpu);

		qemu_fdt_add_subnode(vms->fdt, nodename);
		qemu_fdt_setprop_string(vms->fdt, nodename, "device_type", "cpu");
		qemu_fdt_setprop_string(vms->fdt, nodename, "compatible",
									armcpu->dtb_compatible);

		if (vms->psci_conduit != QEMU_PSCI_CONDUIT_DISABLED && smp_cpus > 1) {
			qemu_fdt_setprop_string(vms->fdt, nodename,
										"enable-method", "psci");
		}

		if (addr_cells == 2) {
			qemu_fdt_setprop_u64(vms->fdt, nodename, "reg",
									armcpu->mp_affinity);
		} else {
			qemu_fdt_setprop_cell(vms->fdt, nodename, "reg",
									armcpu->mp_affinity);
		}

		if (ms->possible_cpus->cpus[cs->cpu_index].props.has_node_id) {
			qemu_fdt_setprop_cell(vms->fdt, nodename, "numa-node-id",
				ms->possible_cpus->cpus[cs->cpu_index].props.node_id);
		}

		qemu_fdt_setprop_cell(vms->fdt, nodename, "phandle",
									qemu_fdt_alloc_phandle(vms->fdt));

		g_free(nodename);
	}

	/*
		* Add vCPU topology description through fdt node cpu-map.
		*
		* See Linux Documentation/devicetree/bindings/cpu/cpu-topology.txt
		* In a SMP system, the hierarchy of CPUs can be defined through
		* four entities that are used to describe the layout of CPUs in
		* the system: socket/cluster/core/thread.
		*
		* A socket node represents the boundary of system physical package
		* and its child nodes must be one or more cluster nodes. A system
		* can contain several layers of clustering within a single physical
		* package and cluster nodes can be contained in parent cluster nodes.
		*
		* Note: currently we only support one layer of clustering within
		* each physical package.
	*/
	qemu_fdt_add_subnode(vms->fdt, "/cpus/cpu-map");

	for (cpu = smp_cpus - 1; cpu >= 0; cpu--) {
		char *cpu_path = g_strdup_printf("/cpus/cpu@%lx",
								ms->possible_cpus->cpus[cpu].arch_id);
		char *map_path;

		if (ms->smp.threads > 1) {
			map_path = g_strdup_printf(
				"/cpus/cpu-map/socket%d/cluster%d/core%d/thread%d",
				cpu / (ms->smp.clusters * ms->smp.cores * ms->smp.threads),
				(cpu / (ms->smp.cores * ms->smp.threads)) % ms->smp.clusters,
				(cpu / ms->smp.threads) % ms->smp.cores,
				cpu % ms->smp.threads);
		} else {
			map_path = g_strdup_printf(
				"/cpus/cpu-map/socket%d/cluster%d/core%d",
				cpu / (ms->smp.clusters * ms->smp.cores),
				(cpu / ms->smp.cores) % ms->smp.clusters,
				cpu % ms->smp.cores);
		}
		qemu_fdt_add_path(vms->fdt, map_path);
		qemu_fdt_setprop_phandle(vms->fdt, map_path, "cpu", cpu_path);

		g_free(map_path);
		g_free(cpu_path);
	}
}

static void fdt_add_gic_node(LagunaVirt *vms)
{
	char *nodename;

	vms->gic_phandle = qemu_fdt_alloc_phandle(vms->fdt);
	qemu_fdt_setprop_cell(vms->fdt, "/", "interrupt-parent", vms->gic_phandle);

	nodename = g_strdup_printf("/gic@%" PRIx64,
								base_memmap[VIRT_GIC_DIST].base);
	qemu_fdt_add_subnode(vms->fdt, nodename);
	qemu_fdt_setprop_cell(vms->fdt, nodename, "#interrupt-cells", 3);
	qemu_fdt_setprop(vms->fdt, nodename, "interrupt-controller", NULL, 0);
	qemu_fdt_setprop_cell(vms->fdt, nodename, "#address-cells", 0x2);
	qemu_fdt_setprop_cell(vms->fdt, nodename, "#size-cells", 0x2);
	qemu_fdt_setprop(vms->fdt, nodename, "ranges", NULL, 0);
	qemu_fdt_setprop_string(vms->fdt, nodename, "compatible",
							"arm,gic");
	qemu_fdt_setprop_cell(vms->fdt, nodename,
							"#redistributor-regions", 1);
	qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
									2, base_memmap[VIRT_GIC_DIST].base,
									2, base_memmap[VIRT_GIC_DIST].size,
									2, base_memmap[VIRT_GIC_CPU].base,
									2, base_memmap[VIRT_GIC_CPU].size);

	if (vms->cfg.virt) {
		qemu_fdt_setprop_cells(vms->fdt, nodename, "interrupts",
								GIC_FDT_IRQ_TYPE_PPI, ARCH_GIC_MAINT_IRQ,
								GIC_FDT_IRQ_FLAGS_LEVEL_HI);
	}

	qemu_fdt_setprop_cell(vms->fdt, nodename, "phandle", vms->gic_phandle);
	g_free(nodename);
}

static void fdt_add_sdhci_nodes(const LagunaVirt *vms)
{
	char *nodename;
	uint32_t nr_sdhci = ARRAY_SIZE(vms->lua.apu.peri.mmc);
	hwaddr base = base_memmap[VIRT_EMMC].base;
	hwaddr size = base_memmap[VIRT_EMMC].size;
	int irq = apu_irqmap[VIRT_EMMC];
	const char compat[] = "axera,lua-dwcmshc";
	int i;

	/* Create nodes in incremental address */
	base = base + size * (nr_sdhci - 1);
	irq = irq + 2 * (nr_sdhci - 1);
	for (i = nr_sdhci - 1; i >= 0; i--) {
		nodename = g_strdup_printf("/soc/sdhci@%" PRIx64, base);
		qemu_fdt_add_subnode(vms->fdt, nodename);
		/* Note that we can't use setprop_string because of the embedded NUL */
		qemu_fdt_setprop(vms->fdt, nodename, "compatible",
							compat, sizeof(compat));
		qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
										2, base, 2, size);
		qemu_fdt_setprop_cells(vms->fdt, nodename, "interrupts",
								GIC_FDT_IRQ_TYPE_SPI, irq,
								GIC_FDT_IRQ_FLAGS_LEVEL_HI);
		qemu_fdt_setprop_cells(vms->fdt, nodename, "sdhci-caps-mask", 0xffffffff, 0xffffffff);
		qemu_fdt_setprop_cells(vms->fdt, nodename, "sdhci-caps", 0x70, 0x156ecc02);

		if (vms->cfg.has_emmc && i == 0) {
			qemu_fdt_setprop(vms->fdt, nodename, "non-removable", NULL, 0);
			qemu_fdt_setprop(vms->fdt, nodename, "no-sdio", NULL, 0);
			qemu_fdt_setprop(vms->fdt, nodename, "no-sd", NULL, 0);
			qemu_fdt_setprop_cell(vms->fdt, nodename, "bus-width", 8);
			qemu_fdt_setprop(vms->fdt, nodename, "cap-mmc-highspeed", NULL, 0);
			qemu_fdt_setprop(vms->fdt, nodename, "mmc-hs200-1_8v", NULL, 0);
		}
		qemu_fdt_setprop_cell(vms->fdt, nodename, "max-frequency", 200000000);
		base -= size;
		irq -= 2;
	}
}

static void fdt_add_aliases_nodes(LagunaVirt *vms)
{
	int i;
	hwaddr base = base_memmap[VIRT_UART1].base;
	hwaddr size = base_memmap[VIRT_UART1].size;
	uint32_t nr_uart = ARRAY_SIZE(vms->lua.apu.peri.uarts) / 2;
	qemu_fdt_add_subnode(vms->fdt, "/aliases");
	char *nodename, *propname;

	for (i = 1; i <= nr_uart; i++) {
		nodename = g_strdup_printf("/serial@%" PRIx64, base);
		propname = g_strdup_printf("serial%d", i);
		qemu_fdt_setprop_string(vms->fdt, "/aliases", propname, nodename);

		base += size;

		g_free(nodename);
		g_free(propname);
	}
}

static void fdt_add_uart_nodes(const LagunaVirt *vms)
{
	char *nodename;
	uint32_t nr_uart = ARRAY_SIZE(vms->lua.apu.peri.uarts) / 2;
	hwaddr base = base_memmap[VIRT_UART1].base;
	hwaddr size = base_memmap[VIRT_UART1].size;
	int irq = apu_irqmap[VIRT_UART1];
	const char compat[] = "ns16550";
	const char clocknames[] = "apb_pclk";
	int i;

	/* Create nodes in incremental address */
	base = base + size * (nr_uart - 1);
	irq = irq + nr_uart - 1;
	for (i = nr_uart; i > 0; i--) {
		nodename = g_strdup_printf("/soc/serial@%" PRIx64, base);
		qemu_fdt_add_subnode(vms->fdt, nodename);
		/* Note that we can't use setprop_string because of the embedded NUL */
		qemu_fdt_setprop(vms->fdt, nodename, "compatible",
							compat, sizeof(compat));
		qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
										2, base, 2, size);
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
		if (i == 1) {
			/* Select UART0 as console  */
			qemu_fdt_setprop_string(vms->fdt, "/chosen", "stdout-path", nodename);
		}

		g_free(nodename);
	}
}

static void *lua_virt_dtb(const struct arm_boot_info *binfo, int *fdt_size)
{
	const LagunaVirt *board = container_of(binfo, LagunaVirt, bootinfo);

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

static void fdt_add_memory_nodes(LagunaVirt *s, void *fdt, uint64_t ram_size)
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

	addr_ranges[0].base = base_memmap[VIRT_MEM].base;
	addr_ranges[0].size = base_memmap[VIRT_MEM].size;

	fdt_nop_memory_nodes(fdt, &err);
	if (err) {
		error_report_err(err);
		return;
	}

	name = g_strdup_printf("/memory@%lx", base_memmap[VIRT_MEM].base);

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
										2, mem_reg_prop[0],
										2, mem_reg_prop[1]);
		break;
	case 2:
		qemu_fdt_setprop_sized_cells(fdt, name, "reg",
										2, mem_reg_prop[0],
										2, mem_reg_prop[1],
										2, mem_reg_prop[2],
										2, mem_reg_prop[3]);
		break;
	default:
		g_assert_not_reached();
	}
	g_free(name);
}

static void lua_virt_modify_dtb(const struct arm_boot_info *binfo, void *fdt)
{
	LagunaVirt *s = container_of(binfo, LagunaVirt, bootinfo);

	fdt_add_memory_nodes(s, fdt, binfo->ram_size);
}

static void lua_virt_mach_done(Notifier *notifier, void *data)
{
	LagunaVirt *vms = container_of(notifier, LagunaVirt,
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
	LagunaVirt *vms = LAGUNA_VIRT_MACHINE(machine);
	MachineClass *mc = MACHINE_GET_CLASS(machine);

	mc->possible_cpu_arch_ids(machine);

	vms->psci_conduit = QEMU_PSCI_CONDUIT_SMC;

	object_initialize_child(OBJECT(machine), "lua-soc", &vms->lua,
							TYPE_LUA_SOC);
	object_property_set_link(OBJECT(&vms->lua), "lua-soc.ddr",
							OBJECT(machine->ram), &error_abort);

	if (vms->cfg.has_emmc)
		object_property_set_bool(OBJECT(&vms->lua), "has-emmc",
								vms->cfg.has_emmc, &error_abort);

	if (vms->cfg.part_config)
		object_property_set_uint(OBJECT(&vms->lua), "part-config",
							vms->cfg.part_config, &error_abort);

	if (vms->cfg.bootmode)
		object_property_set_uint(OBJECT(&vms->lua), "bootmode",
							vms->cfg.bootmode, &error_abort);

	if (vms->cfg.virt)
		object_property_set_bool(OBJECT(&vms->lua), "virtualization",
								vms->cfg.virt, &error_abort);

	if(vms->cfg.secure)
		object_property_set_bool(OBJECT(&vms->lua), "secure",
								vms->cfg.secure, &error_abort);

	if(vms->cfg.nor_flash)
		object_property_set_str(OBJECT(&vms->lua), "nor-flash",
								vms->cfg.nor_flash, &error_abort);

	if (vms->cfg.download)
		object_property_set_bool(OBJECT(&vms->lua), "download",
								vms->cfg.download, &error_abort);

	if (vms->cfg.match)
		object_property_set_bool(OBJECT(&vms->lua), "match",
								vms->cfg.match, &error_abort);

	if (vms->cfg.bootstrap)
		object_property_set_uint(OBJECT(&vms->lua), "bootstrap",
								vms->cfg.bootstrap, &error_abort);

	sysbus_realize_and_unref(SYS_BUS_DEVICE(&vms->lua), &error_fatal);

	create_fdt(vms);
	fdt_add_clk_nodes(vms);
	fdt_add_cpu_nodes(vms);
	fdt_add_gic_node(vms);
	fdt_add_timer_nodes(vms);
	fdt_add_uart_nodes(vms);
	fdt_add_sdhci_nodes(vms);
	fdt_add_aliases_nodes(vms);

	vms->bootinfo.ram_size = machine->ram_size;
	vms->bootinfo.board_id = -1;
	vms->bootinfo.loader_start = base_memmap[VIRT_MEM].base;
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
	LagunaVirt *vms = LAGUNA_VIRT_MACHINE(obj);
	MachineState *ms = MACHINE(vms);

	/* default spi nor flash model */
	vms->cfg.nor_flash = g_strdup("n25q032a11");

	/* default spi nand flash model */
	vms->cfg.nand = "TC58CVG2S0HRAIG";

	ms->smp.cores = LUA_SOC_CLUSTER_SIZE;
	ms->smp.clusters = LUA_SOC_CLUSTERS;
}

static void lua_virt_mach_class_init(ObjectClass *oc, void *data)
{
	MachineClass *mc = MACHINE_CLASS(oc);

	mc->desc = "Laguna SoC Virtual Platform";
	mc->init = lua_virt_mach_init;
	mc->min_cpus = LUA_SOC_NR_ACPUS;
	mc->max_cpus = LUA_SOC_NR_ACPUS;
	mc->minimum_page_bits = 12;
	mc->possible_cpu_arch_ids = lua_virt_possible_cpu_arch_ids;
	mc->default_cpus = LUA_SOC_NR_ACPUS;
	mc->default_cpu_type = LUA_SOC_ACPU_TYPE;
	mc->no_cdrom = 1;
	mc->no_sdcard = 1;
	mc->no_floppy = 1;
	mc->block_default_type = IF_NONE;
	mc->default_ram_id = "lua-soc.ddr";

	object_class_property_add_bool(oc, "emmc", NULL,
					lua_virt_set_emmc);
	object_class_property_add_bool(oc, "virt", NULL,
					lua_virt_set_virt);
	object_class_property_add_bool(oc, "secure", NULL,
					lua_virt_set_secure);
	object_class_property_add(oc, "part-config", "uint8",
		NULL, lua_virt_set_part_config,
		NULL, NULL);
	object_class_property_add(oc, "bootmode", "uint8",
		NULL, lua_virt_set_bootmode,
		NULL, NULL);
	object_class_property_add_str(oc, "nor", NULL,
					lua_virt_set_nor_flash);
	object_class_property_add_bool(oc, "download", NULL,
					lua_virt_set_download);
	object_class_property_add_bool(oc, "match", NULL,
					lua_virt_set_match);
	object_class_property_add(oc, "bootstrap", "uint32",
		NULL, lua_virt_set_bootstrap,
		NULL, NULL);
}

static void lua_virt_mach_finalize(Object *obj)
{
	LagunaVirt *vms = LAGUNA_VIRT_MACHINE(obj);

	g_free((void *)vms->cfg.nor_flash);
}

static const TypeInfo lua_virt_mach_info = {
	.name       = TYPE_LAGUNA_VIRT_MACHINE,
	.parent     = TYPE_MACHINE,
	.class_init = lua_virt_mach_class_init,
	.instance_init = lua_virt_mach_instance_init,
	.instance_size = sizeof(LagunaVirt),
	.instance_finalize = lua_virt_mach_finalize,
};

static void lua_virt_machine_init(void)
{
	type_register_static(&lua_virt_mach_info);
}

type_init(lua_virt_machine_init)
