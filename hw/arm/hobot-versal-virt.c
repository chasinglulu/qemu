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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/arm/fdt.h"
#include "cpu.h"
#include "hw/qdev-properties.h"
#include "hw/arm/versal-sigi.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"

#define TYPE_HOBOT_VERSAL_VIRT_MACHINE MACHINE_TYPE_NAME("hobot-sigi-virt")
OBJECT_DECLARE_SIMPLE_TYPE(HobotVersalVirt, HOBOT_VERSAL_VIRT_MACHINE)

struct HobotVersalVirt {
    MachineState parent_obj;
    Notifier machine_done;

    SigiVirt soc;

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
    } cfg;
};

static void hobot_versal_virt_set_emmc(Object *obj, bool value, Error **errp)
{
    HobotVersalVirt *s = HOBOT_VERSAL_VIRT_MACHINE(obj);

    s->cfg.has_emmc = value;
}

static void hobot_versal_virt_set_virt(Object *obj, bool value, Error **errp)
{
    HobotVersalVirt *s = HOBOT_VERSAL_VIRT_MACHINE(obj);

    s->cfg.virt = value;
}

static void hobot_versal_virt_set_secure(Object *obj, bool value, Error **errp)
{
    HobotVersalVirt *s = HOBOT_VERSAL_VIRT_MACHINE(obj);

    s->cfg.secure = value;
}

static const CPUArchIdList *virt_possible_cpu_arch_ids(MachineState *ms)
{
    int n;
    unsigned int max_cpus = ms->smp.max_cpus;
    HobotVersalVirt *vms = HOBOT_VERSAL_VIRT_MACHINE(ms);
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
            virt_cpu_mp_affinity(n);

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

static void create_fdt(HobotVersalVirt *s)
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
    qemu_fdt_setprop_string(s->fdt, "/", "compatible", "hobot-versal-virt");
}

static void fdt_add_timer_nodes(const HobotVersalVirt *vms)
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

static void fdt_add_cpu_nodes(const HobotVersalVirt *vms)
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

static void create_pcie_irq_map(const HobotVersalVirt *vms,
                                uint32_t gic_phandle,
                                int first_irq, const char *nodename)
{
    int devfn, pin;
    uint32_t full_irq_map[4 * 4 * 10] = { 0 };
    uint32_t *irq_map = full_irq_map;

    for (devfn = 0; devfn <= 0x18; devfn += 0x8) {
        for (pin = 0; pin < 4; pin++) {
            int irq_type = GIC_FDT_IRQ_TYPE_SPI;
            int irq_nr = first_irq + ((pin + PCI_SLOT(devfn)) % PCI_NUM_PINS);
            int irq_level = GIC_FDT_IRQ_FLAGS_LEVEL_HI;
            int i;

            uint32_t map[] = {
                devfn << 8, 0, 0,                           /* devfn */
                pin + 1,                                    /* PCI pin */
                gic_phandle, 0, 0, irq_type, irq_nr, irq_level }; /* GIC irq */

            /* Convert map to big endian */
            for (i = 0; i < 10; i++) {
                irq_map[i] = cpu_to_be32(map[i]);
            }
            irq_map += 10;
        }
    }

    qemu_fdt_setprop(vms->fdt, nodename, "interrupt-map",
                     full_irq_map, sizeof(full_irq_map));

    qemu_fdt_setprop_cells(vms->fdt, nodename, "interrupt-map-mask",
                           cpu_to_be16(PCI_DEVFN(3, 0)), /* Slot 3 */
                           0, 0,
                           0x7           /* PCI irq */);
}

static void fdt_add_pcie_node(HobotVersalVirt *vms, int pcie)
{
    char *nodename;
    hwaddr base = base_memmap[pcie].base;
    hwaddr size = base_memmap[pcie].size;
    int irq = a78irqmap[pcie];
    int nr_pcie_buses = size / PCIE_MMCFG_SIZE_MIN;

    nodename = g_strdup_printf("/soc/pcie@%" PRIx64, base);
    qemu_fdt_add_subnode(vms->fdt, nodename);
    qemu_fdt_setprop_string(vms->fdt, nodename,
                            "compatible", "pci-host-ecam-generic");
    qemu_fdt_setprop_string(vms->fdt, nodename, "device_type", "pci");
    qemu_fdt_setprop_cell(vms->fdt, nodename, "#address-cells", 3);
    qemu_fdt_setprop_cell(vms->fdt, nodename, "#size-cells", 2);
    qemu_fdt_setprop_cell(vms->fdt, nodename, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(vms->fdt, nodename, "bus-range", 0,
                           nr_pcie_buses - 1);
    qemu_fdt_setprop(vms->fdt, nodename, "dma-coherent", NULL, 0);

    if (vms->msi_phandle) {
        qemu_fdt_setprop_cells(vms->fdt, nodename, "msi-parent",
                               vms->msi_phandle);
    }

    qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg", 2, base, 2, size);

    qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "ranges",
                                     1, FDT_PCI_RANGE_MMIO, 2, base_memmap[VIRT_PCIE_MMIO].base,
                                     2, base_memmap[VIRT_PCIE_MMIO].base,
                                     2, base_memmap[VIRT_PCIE_MMIO].size,
                                     1, FDT_PCI_RANGE_MMIO_64BIT,
                                     2, base_memmap[VIRT_PCIE_MMIO_HIGH].base,
                                     2, base_memmap[VIRT_PCIE_MMIO_HIGH].base,
                                     2, base_memmap[VIRT_PCIE_MMIO_HIGH].base);

    qemu_fdt_setprop_cell(vms->fdt, nodename, "#interrupt-cells", 1);
    create_pcie_irq_map(vms, vms->gic_phandle, irq, nodename);
}

static void fdt_add_gic_node(HobotVersalVirt *vms)
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
                            "arm,gic-v3");
    qemu_fdt_setprop_cell(vms->fdt, nodename,
                            "#redistributor-regions", 1);
    qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
                                    2, base_memmap[VIRT_GIC_DIST].base,
                                    2, base_memmap[VIRT_GIC_DIST].size,
                                    2, base_memmap[VIRT_GIC_REDIST].base,
                                    2, base_memmap[VIRT_GIC_REDIST].size);

    if (vms->cfg.virt) {
        qemu_fdt_setprop_cells(vms->fdt, nodename, "interrupts",
                                GIC_FDT_IRQ_TYPE_PPI, ARCH_GIC_MAINT_IRQ,
                                GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    }

    qemu_fdt_setprop_cell(vms->fdt, nodename, "phandle", vms->gic_phandle);
    g_free(nodename);
}

static void fdt_add_gic_its_node(HobotVersalVirt *vms)
{
    char *nodename;

    vms->msi_phandle = qemu_fdt_alloc_phandle(vms->fdt);
    nodename = g_strdup_printf("/gic@%" PRIx64 "/its@%" PRIx64,
                                base_memmap[VIRT_GIC_DIST].base,
                                base_memmap[VIRT_GIC_ITS].base);
    qemu_fdt_add_subnode(vms->fdt, nodename);
    qemu_fdt_setprop_string(vms->fdt, nodename, "compatible",
                            "arm,gic-v3-its");
    qemu_fdt_setprop(vms->fdt, nodename, "msi-controller", NULL, 0);
    qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
                                2, base_memmap[VIRT_GIC_ITS].base,
                                2, base_memmap[VIRT_GIC_ITS].size);
    qemu_fdt_setprop_cell(vms->fdt, nodename, "phandle", vms->msi_phandle);
    g_free(nodename);
}

static void fdt_add_clk_nodes(HobotVersalVirt *vms)
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

static void fdt_add_aliases_nodes(HobotVersalVirt *vms)
{
    int i;
    hwaddr base = base_memmap[VIRT_UART].base;
    hwaddr size = base_memmap[VIRT_UART].size;
    qemu_fdt_add_subnode(vms->fdt, "/aliases");
    char *nodename, *propname;

    for (i = 0; i < ARRAY_SIZE(vms->soc.apu.peri.uarts); i++) {
        nodename = g_strdup_printf("/serial@%" PRIx64, base);
        propname = g_strdup_printf("serial%d", i);
        qemu_fdt_setprop_string(vms->fdt, "/aliases", propname, nodename);

        base += size;

        g_free(nodename);
        g_free(propname);
    }
}

static void fdt_add_usb_nodes(const HobotVersalVirt *vms)
{
    char *nodename;
    hwaddr ctrl_base = base_memmap[VIRT_USB_CTRL].base;
    hwaddr ctrl_size = base_memmap[VIRT_USB_CTRL].size;
    hwaddr base = base_memmap[VIRT_DWC_USB].base;
    hwaddr size = base_memmap[VIRT_DWC_USB].size;
    int irq = a78irqmap[VIRT_DWC_USB];
    const char ctrl_compat[] = "hobot,sigi-dwc3";
    const char compat[] = "snps,dwc3";

    nodename = g_strdup_printf("/soc/usb@%" PRIx64, ctrl_base);
    qemu_fdt_add_subnode(vms->fdt, nodename);
    qemu_fdt_setprop(vms->fdt, nodename, "compatible",
                            ctrl_compat, sizeof(ctrl_compat));
    qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
                                2, ctrl_base, 2, ctrl_size);
    qemu_fdt_setprop_cell(vms->fdt, nodename, "#address-cells", 2);
    qemu_fdt_setprop_cell(vms->fdt, nodename, "#size-cells", 2);
    qemu_fdt_setprop(vms->fdt, nodename, "ranges", NULL, 0);
    g_free(nodename);


    nodename = g_strdup_printf("/soc/usb@%" PRIx64 "/dwc_usb@%" PRIx64, ctrl_base, base);
    qemu_fdt_add_subnode(vms->fdt, nodename);
    qemu_fdt_setprop(vms->fdt, nodename, "compatible",
                            compat, sizeof(compat));
    qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
                                2, base, 2, size);
    qemu_fdt_setprop_cell(vms->fdt, nodename, "#stream-id-cells", 1);
    qemu_fdt_setprop_cells(vms->fdt, nodename, "interrupts",
                                GIC_FDT_IRQ_TYPE_SPI, irq,
                                GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop_string(vms->fdt, nodename, "interrupt-names", "dwc_usb3");
    qemu_fdt_setprop_cell(vms->fdt, nodename, "snps,quirk-frame-length-adjustment", 0x20);
    qemu_fdt_setprop(vms->fdt, nodename, "snps,refclk_fladj", NULL, 0);
    qemu_fdt_setprop(vms->fdt, nodename, "snps,enable_guctl1_resume_quirk", NULL, 0);
    qemu_fdt_setprop(vms->fdt, nodename, "snps,enable_guctl1_ipd_quirk", NULL, 0);
    qemu_fdt_setprop(vms->fdt, nodename, "snps,xhci-stream-quirk", NULL, 0);
    qemu_fdt_setprop_string(vms->fdt, nodename, "dr_mode", "host");
    qemu_fdt_setprop_string(vms->fdt, nodename, "phy-names", "usb3-phy");
    g_free(nodename);
}

static void fdt_add_gpio_nodes(const HobotVersalVirt *vms, int gpio)
{
    char *nodename, *portname, *bankname;
    hwaddr base = base_memmap[gpio].base;
    hwaddr size = base_memmap[gpio].size;
    int irq = a78irqmap[gpio];
    const char compat[] = "snps,dw-apb-gpio";
    const char port_compat[] = "snps,dw-apb-gpio-port";
    int i, j;

    for (i = 0; i < ARRAY_SIZE(vms->soc.apu.peri.gpio); i++) {
        nodename = g_strdup_printf("/soc/gpio@%" PRIx64, base);
        qemu_fdt_add_subnode(vms->fdt, nodename);
        qemu_fdt_setprop(vms->fdt, nodename, "compatible",
                            compat, sizeof(compat));
        qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
                                        2, base, 2, size);
        qemu_fdt_setprop_cell(vms->fdt, nodename, "#address-cells", 1);
        qemu_fdt_setprop_cell(vms->fdt, nodename, "#size-cells", 0);

        /* 4 port bank per gpio controller */
        for (j = 0; j < 4; j++) {
            portname = g_strdup_printf("/gpio@%" PRIx64 "/port@%d", base, j);
            bankname = g_strdup_printf("gpio%d_%c", i, 'a' + j);
            qemu_fdt_add_path(vms->fdt, portname);
            qemu_fdt_setprop(vms->fdt, portname, "compatible",
                                port_compat, sizeof(port_compat));
            qemu_fdt_setprop(vms->fdt, portname, "gpio-controller", NULL, 0);
            qemu_fdt_setprop_cell(vms->fdt, portname, "#gpio-cells", 2);
            qemu_fdt_setprop_cell(vms->fdt, portname, "snps,nr-gpios", 32);
            qemu_fdt_setprop_sized_cells(vms->fdt, portname, "reg",1, j);
            qemu_fdt_setprop_string(vms->fdt, portname, "bank-name", bankname);

            /* GPIO port A as interrupt */
            if (j == 0) {
                qemu_fdt_setprop(vms->fdt, portname, "interrupt-controller", NULL, 0);
                qemu_fdt_setprop_cell(vms->fdt, portname, "#interrupt-cells", 2);
                qemu_fdt_setprop_cells(vms->fdt, portname, "interrupts",
                                            GIC_FDT_IRQ_TYPE_SPI, irq,
                                            GIC_FDT_IRQ_FLAGS_LEVEL_HI);
            }

            g_free(bankname);
            g_free(portname);
        }

        base += size;
        irq++;
        g_free(nodename);
    }
}

static void fdt_add_sdhci_nodes(const HobotVersalVirt *vms, int sdhci)
{
    char *nodename;
    uint32_t nr_sdhci = ARRAY_SIZE(vms->soc.apu.peri.mmc);
    hwaddr base = base_memmap[sdhci].base;
    hwaddr size = base_memmap[sdhci].size;
    int irq = a78irqmap[sdhci];
    const char compat[] = "cdns,sd4hc";
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
        qemu_fdt_setprop_cell(vms->fdt, nodename, "clocks", vms->clock_phandle);
        qemu_fdt_setprop_cells(vms->fdt, nodename, "sdhci-caps-mask", 0xffffffff, 0xffffffff);
        qemu_fdt_setprop_cells(vms->fdt, nodename, "sdhci-caps", 0x70, 0x156ac800);

        if (vms->cfg.has_emmc && i == 0) {
            qemu_fdt_setprop(vms->fdt, nodename, "non-removable", NULL, 0);
            qemu_fdt_setprop(vms->fdt, nodename, "no-sdio", NULL, 0);
            qemu_fdt_setprop(vms->fdt, nodename, "no-sd", NULL, 0);
            qemu_fdt_setprop_cell(vms->fdt, nodename, "bus-width", 8);
            qemu_fdt_setprop(vms->fdt, nodename, "cap-mmc-highspeed", NULL, 0);
            qemu_fdt_setprop(vms->fdt, nodename, "mmc-hs200-1_8v", NULL, 0);
        }
        qemu_fdt_setprop_cell(vms->fdt, nodename, "max-frequency", 200000000);
        qemu_fdt_setprop_cell(vms->fdt, nodename, "cdns,phy-input-delay-sd-default", 8);
		qemu_fdt_setprop_cell(vms->fdt, nodename, "cdns,phy-input-delay-mmc-highspeed", 3);
        qemu_fdt_setprop_cell(vms->fdt, nodename, "cdns,phy-input-delay-mmc-ddr", 3);
        qemu_fdt_setprop_cell(vms->fdt, nodename, "cdns,phy-dll-delay-strobe", 33);
        qemu_fdt_setprop_cell(vms->fdt, nodename, "cdns,phy-dll-delay-sdclk", 45);
        qemu_fdt_setprop_cell(vms->fdt, nodename, "cdns,phy-dll-delay-sdclk-hsmmc", 45);

        base -= size;
        irq -= 2;
    }
}

static void fdt_add_flash_node(const HobotVersalVirt *vms, int flash)
{
    hwaddr flashsize = base_memmap[flash].size / 2;
    hwaddr flashbase = base_memmap[flash].base;
    char *nodename;

    /* Report both flash devices as a single node in the DT */
    nodename = g_strdup_printf("/soc/flash@%" PRIx64, flashbase);
    qemu_fdt_add_subnode(vms->fdt, nodename);
    qemu_fdt_setprop_string(vms->fdt, nodename, "compatible", "cfi-flash");
    qemu_fdt_setprop_sized_cells(vms->fdt, nodename, "reg",
                                    2, flashbase, 2, flashsize,
                                    2, flashbase + flashsize, 2, flashsize);
    qemu_fdt_setprop_cell(vms->fdt, nodename, "bank-width", 4);
    g_free(nodename);
}

static void fdt_add_uart_nodes(const HobotVersalVirt *vms, int uart)
{
    char *nodename;
    uint32_t nr_uart = ARRAY_SIZE(vms->soc.apu.peri.uarts);
    hwaddr base = base_memmap[uart].base;
    hwaddr size = base_memmap[uart].size;
    int irq = a78irqmap[uart];
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
        base -= size;
        irq -= 1;
        if (i == 0) {
            /* Select UART0 as console  */
            qemu_fdt_setprop_string(vms->fdt, "/chosen", "stdout-path", nodename);
        }

        g_free(nodename);
    }
}

static void *hobot_versal_virt_dtb(const struct arm_boot_info *binfo, int *fdt_size)
{
    const HobotVersalVirt *board = container_of(binfo, HobotVersalVirt,
                                                 bootinfo);

    *fdt_size = board->fdt_size;
    return board->fdt;
}

static void hobot_versal_virt_mach_done(Notifier *notifier, void *data)
{
    HobotVersalVirt *vms = container_of(notifier, HobotVersalVirt,
                                         machine_done);
    MachineState *ms = MACHINE(vms);
    ARMCPU *cpu = ARM_CPU(first_cpu);
    struct arm_boot_info *info = &vms->bootinfo;
    AddressSpace *as = arm_boot_address_space(cpu, info);

    if (arm_load_dtb(info->dtb_start, info, info->dtb_limit, as, ms) < 0) {
        exit(1);
    }
}

static void hobot_versal_virt_mach_init(MachineState *machine)
{
    HobotVersalVirt *vms = HOBOT_VERSAL_VIRT_MACHINE(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    mc->possible_cpu_arch_ids(machine);

    vms->psci_conduit = QEMU_PSCI_CONDUIT_SMC;

    object_initialize_child(OBJECT(machine), "sigi-virt", &vms->soc,
                            TYPE_SIGI_VIRT);
    object_property_set_link(OBJECT(&vms->soc), "sigi-virt.ddr",
                            OBJECT(machine->ram), &error_abort);

    if (vms->cfg.has_emmc)
        object_property_set_bool(OBJECT(&vms->soc), "has-emmc",
                                vms->cfg.has_emmc, &error_abort);

    if (vms->cfg.virt)
        object_property_set_bool(OBJECT(&vms->soc), "virtualization",
                                vms->cfg.virt, &error_abort);

    if(vms->cfg.secure)
        object_property_set_bool(OBJECT(&vms->soc), "secure",
                                vms->cfg.secure, &error_abort);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(&vms->soc), &error_fatal);

    create_fdt(vms);
    fdt_add_clk_nodes(vms);
    fdt_add_cpu_nodes(vms);
    fdt_add_gic_node(vms);
    fdt_add_gic_its_node(vms);
    fdt_add_timer_nodes(vms);
    fdt_add_uart_nodes(vms, VIRT_UART);
    fdt_add_gpio_nodes(vms, VIRT_GPIO);
    fdt_add_pcie_node(vms, VIRT_PCIE_ECAM);
    fdt_add_usb_nodes(vms);
    fdt_add_sdhci_nodes(vms, VIRT_SDHCI);
    fdt_add_flash_node(vms, VIRT_FLASH);
    fdt_add_aliases_nodes(vms);

    vms->bootinfo.ram_size = machine->ram_size;
    vms->bootinfo.board_id = -1;
    vms->bootinfo.loader_start = base_memmap[VIRT_MEM].base;
    vms->bootinfo.get_dtb = hobot_versal_virt_dtb;
    vms->bootinfo.skip_dtb_autoload = true;
    vms->bootinfo.psci_conduit = vms->psci_conduit;
    arm_load_kernel(ARM_CPU(first_cpu), machine, &vms->bootinfo);

    vms->machine_done.notify = hobot_versal_virt_mach_done;
    qemu_add_machine_init_done_notifier(&vms->machine_done);
}

static void hobot_versal_virt_mach_instance_init(Object *obj)
{
    HobotVersalVirt *vms = HOBOT_VERSAL_VIRT_MACHINE(obj);
    MachineState *ms = MACHINE(vms);

    ms->smp.cores = SIGI_VIRT_CLUSTER_SIZE;
    ms->smp.clusters = SIGI_VIRT_NR_ACPUS % SIGI_VIRT_CLUSTER_SIZE;
    ms->smp.clusters = ms->smp.clusters ? SIGI_VIRT_NR_ACPUS / SIGI_VIRT_CLUSTER_SIZE + 1 :
                                          SIGI_VIRT_NR_ACPUS / SIGI_VIRT_CLUSTER_SIZE;
}

static void hobot_versal_virt_mach_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Horizon Robotics Jounery Development Board";
    mc->init = hobot_versal_virt_mach_init;
    mc->min_cpus = SIGI_VIRT_NR_ACPUS;
    mc->max_cpus = 16;
    mc->minimum_page_bits = 12;
    mc->possible_cpu_arch_ids = virt_possible_cpu_arch_ids;
    mc->default_cpus = SIGI_VIRT_NR_ACPUS;
    mc->no_cdrom = 1;
    mc->no_sdcard = 1;
    mc->no_floppy = 1;
    mc->block_default_type = IF_EMMC;
    mc->default_ram_id = "sigi-virt.ddr";

    object_class_property_add_bool(oc, "emmc", NULL,
		            hobot_versal_virt_set_emmc);
    object_class_property_add_bool(oc, "virt", NULL,
		            hobot_versal_virt_set_virt);
    object_class_property_add_bool(oc, "secure", NULL,
		            hobot_versal_virt_set_secure);
}

static const TypeInfo hobot_versal_virt_mach_info = {
    .name       = TYPE_HOBOT_VERSAL_VIRT_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = hobot_versal_virt_mach_class_init,
    .instance_init = hobot_versal_virt_mach_instance_init,
    .instance_size = sizeof(HobotVersalVirt),
};

static void hobot_versal_virt_machine_init(void)
{
    type_register_static(&hobot_versal_virt_mach_info );
}

type_init(hobot_versal_virt_machine_init)
