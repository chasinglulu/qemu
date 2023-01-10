/*
 * Hobot Sigi Virtual Development Board.
 *
 * Copyright (C) 2022 Hobot Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
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
#include "hw/arm/sigi-soc.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include <stdbool.h>
#include <stdint.h>

#define TYPE_HOBOT_SIGI_VIRT_MACHINE MACHINE_TYPE_NAME("hobot-sigi-virt")
OBJECT_DECLARE_SIMPLE_TYPE(HobotSigiVirt, HOBOT_SIGI_VIRT_MACHINE)

struct HobotSigiVirt {
    MachineState parent_obj;
    Notifier machine_done;

    SigiSoC soc;

    void *fdt;
    int fdt_size;
    struct {
        uint32_t gic;
        uint32_t ethernet_phy[2];
        uint32_t clk_125Mhz;
        uint32_t clk_25Mhz;
        uint32_t clk_200Mhz;
        uint32_t usb;
        uint32_t dwc;
    } phandle;
    struct arm_boot_info binfo;

    struct {
        bool secure;
        bool has_emmc;
    } cfg;
};

static void sigi_virt_set_emmc(Object *obj, bool value, Error **errp)
{
    HobotSigiVirt *s = HOBOT_SIGI_VIRT_MACHINE(obj);

    s->cfg.has_emmc = value;
}

static void fdt_create(HobotSigiVirt *s)
{
    MachineClass *mc = MACHINE_GET_CLASS(s);
    int i;

    s->fdt = create_device_tree(&s->fdt_size);
    if (!s->fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    /* Allocate all phandles.  */
    s->phandle.gic = qemu_fdt_alloc_phandle(s->fdt);
    for (i = 0; i < ARRAY_SIZE(s->phandle.ethernet_phy); i++) {
        s->phandle.ethernet_phy[i] = qemu_fdt_alloc_phandle(s->fdt);
    }
    s->phandle.clk_25Mhz = qemu_fdt_alloc_phandle(s->fdt);
    s->phandle.clk_125Mhz = qemu_fdt_alloc_phandle(s->fdt);
    s->phandle.clk_200Mhz = qemu_fdt_alloc_phandle(s->fdt);

    s->phandle.usb = qemu_fdt_alloc_phandle(s->fdt);
    s->phandle.dwc = qemu_fdt_alloc_phandle(s->fdt);
    /* Create /chosen node for load_dtb.  */
    qemu_fdt_add_subnode(s->fdt, "/chosen");

    /* Header */
    qemu_fdt_setprop_cell(s->fdt, "/", "interrupt-parent", s->phandle.gic);
    qemu_fdt_setprop_cell(s->fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(s->fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_string(s->fdt, "/", "model", mc->desc);
    qemu_fdt_setprop_string(s->fdt, "/", "compatible", "hobot-versal-virt");
}

static void fdt_add_clk_node(HobotSigiVirt *s, const char *name,
                             unsigned int freq_hz, uint32_t phandle)
{
    qemu_fdt_add_subnode(s->fdt, name);
    qemu_fdt_setprop_cell(s->fdt, name, "phandle", phandle);
    qemu_fdt_setprop_cell(s->fdt, name, "clock-frequency", freq_hz);
    qemu_fdt_setprop_cell(s->fdt, name, "#clock-cells", 0x0);
    qemu_fdt_setprop_string(s->fdt, name, "compatible", "fixed-clock");
    qemu_fdt_setprop(s->fdt, name, "u-boot,dm-pre-reloc", NULL, 0);
}

static void fdt_add_cpu_nodes(HobotSigiVirt *s, uint32_t psci_conduit)
{
    int i;

    qemu_fdt_add_subnode(s->fdt, "/cpus");
    qemu_fdt_setprop_cell(s->fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(s->fdt, "/cpus", "#address-cells", 1);

    for (i = SIGI_SOC_NR_ACPUS - 1; i >= 0; i--) {
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(i));
        char *name = g_strdup_printf("/cpus/cpu@%lx", armcpu->mp_affinity);

        qemu_fdt_add_subnode(s->fdt, name);
        qemu_fdt_setprop_cell(s->fdt, name, "reg", armcpu->mp_affinity);
        if (psci_conduit != QEMU_PSCI_CONDUIT_DISABLED) {
            qemu_fdt_setprop_string(s->fdt, name, "enable-method", "psci");
        }
        qemu_fdt_setprop_string(s->fdt, name, "device_type", "cpu");
        qemu_fdt_setprop_string(s->fdt, name, "compatible",
                                armcpu->dtb_compatible);
        g_free(name);
    }
}

static void fdt_add_gic_nodes(HobotSigiVirt *s)
{
    char *nodename;

    nodename = g_strdup_printf("/gic@%x", MM_GIC_APU_DIST_MAIN);
    qemu_fdt_add_subnode(s->fdt, nodename);
    qemu_fdt_setprop_cell(s->fdt, nodename, "phandle", s->phandle.gic);
    qemu_fdt_setprop_cells(s->fdt, nodename, "interrupts",
                           GIC_FDT_IRQ_TYPE_PPI, SIGI_SOC_GIC_MAINT_IRQ,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop(s->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_sized_cells(s->fdt, nodename, "reg",
                                 2, MM_GIC_APU_DIST_MAIN,
                                 2, MM_GIC_APU_DIST_MAIN_SIZE,
                                 2, MM_GIC_APU_REDIST_0,
                                 2, MM_GIC_APU_REDIST_0_SIZE);
    qemu_fdt_setprop_cell(s->fdt, nodename, "#interrupt-cells", 3);
    qemu_fdt_setprop_string(s->fdt, nodename, "compatible", "arm,gic-v3");
    g_free(nodename);
}

static void fdt_add_timer_nodes(HobotSigiVirt *s)
{
    const char compat[] = "arm,armv8-timer";
    uint32_t irqflags = GIC_FDT_IRQ_FLAGS_LEVEL_HI;

    qemu_fdt_add_subnode(s->fdt, "/timer");
    qemu_fdt_setprop_cells(s->fdt, "/timer", "interrupts",
            GIC_FDT_IRQ_TYPE_PPI, SIGI_SOC_TIMER_S_EL1_IRQ, irqflags,
            GIC_FDT_IRQ_TYPE_PPI, SIGI_SOC_TIMER_NS_EL1_IRQ, irqflags,
            GIC_FDT_IRQ_TYPE_PPI, SIGI_SOC_TIMER_VIRT_IRQ, irqflags,
            GIC_FDT_IRQ_TYPE_PPI, SIGI_SOC_TIMER_NS_EL2_IRQ, irqflags);
    qemu_fdt_setprop(s->fdt, "/timer", "compatible",
                     compat, sizeof(compat));
}

static void fdt_add_uart_nodes(HobotSigiVirt *s)
{
    const char compat[] = "ns16550";
    uint64_t addr;
    char *name;
    int i;

    for (i = 0; i < ARRAY_SIZE(s->soc.cpu_subsys.peri.uarts); i++) {
        addr = MM_PERI_UART0 + i * MM_PERI_UART0_SIZE;
        name = g_strdup_printf("/uart@%" PRIx64, addr);
        qemu_fdt_add_subnode(s->fdt, name);
        qemu_fdt_setprop_cell(s->fdt, name, "current-speed", 115200);
        qemu_fdt_setprop_cell(s->fdt, name, "clock-frequency", 192000000);
        qemu_fdt_setprop_cell(s->fdt, name, "reg-io-width", 4);
        qemu_fdt_setprop_cell(s->fdt, name, "reg-shift", 2);

        qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, SIGI_SOC_UART0_IRQ_0 + i,
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);

        qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                     2, addr, 2, MM_PERI_UART0_SIZE);
        qemu_fdt_setprop(s->fdt, name, "compatible",
                         compat, sizeof(compat));
        qemu_fdt_setprop(s->fdt, name, "u-boot,dm-pre-reloc", NULL, 0);

        if (addr == MM_PERI_UART0) {
            /* Select UART0.  */
            qemu_fdt_setprop_string(s->fdt, "/chosen", "stdout-path", name);
        }
        g_free(name);
    }
}

static void fdt_add_sdhci_nodes(HobotSigiVirt *s)
{
    const char compat[] = "cdns,sd4hc";
    int i;

    for (i = ARRAY_SIZE(s->soc.cpu_subsys.peri.mmc) - 1; i >= 0; i--) {
        uint64_t addr = MM_PERI_SDHCI0 + MM_PERI_SDHCI0_SIZE * i;
        char *name = g_strdup_printf("/sdhci@%" PRIx64, addr);

        qemu_fdt_add_subnode(s->fdt, name);

        qemu_fdt_setprop_cells(s->fdt, name, "sdhci-caps-mask",
                                        0xffffffff, 0xffffffff);
        qemu_fdt_setprop_cells(s->fdt, name, "sdhci-caps",
                                        0x00002807, 0x37ec6481);
        qemu_fdt_setprop_cell(s->fdt, name, "clocks",
                               s->phandle.clk_200Mhz);
        qemu_fdt_setprop_cells(s->fdt, name, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, SIGI_SOC_SDHCI0_IRQ_0 + i * 2,
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);

        qemu_fdt_setprop_sized_cells(s->fdt, name, "reg",
                                     2, addr, 2, MM_PERI_SDHCI0_SIZE);
        qemu_fdt_setprop(s->fdt, name, "compatible", compat, sizeof(compat));
        /*
         * eMMC specific properties
         */
        if (s->cfg.has_emmc && i == 0) {
            qemu_fdt_setprop(s->fdt, name, "non-removable", NULL, 0);
            qemu_fdt_setprop(s->fdt, name, "no-sdio", NULL, 0);
            qemu_fdt_setprop(s->fdt, name, "no-sd", NULL, 0);
            qemu_fdt_setprop_sized_cells(s->fdt, name, "bus-width", 1, 8);
        }
        g_free(name);
    }
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

static void fdt_add_memory_nodes(HobotSigiVirt *s, void *fdt, uint64_t ram_size)
{
    /* Describes the various split DDR access regions.  */
    static const struct {
        uint64_t base;
        uint64_t size;
    } addr_ranges[] = {
        { MM_TOP_DDR, MM_TOP_DDR_SIZE },
    };
    uint64_t mem_reg_prop[8] = {0};
    uint64_t size = ram_size;
    Error *err = NULL;
    char *name;
    int i;

    fdt_nop_memory_nodes(fdt, &err);
    if (err) {
        error_report_err(err);
        return;
    }

    name = g_strdup_printf("/memory@%llx", MM_TOP_DDR);
    for (i = 0; i < ARRAY_SIZE(addr_ranges) && size; i++) {
        uint64_t mapsize;

        mapsize = size < addr_ranges[i].size ? size : addr_ranges[i].size;

        mem_reg_prop[i * 2] = addr_ranges[i].base;
        mem_reg_prop[i * 2 + 1] = mapsize;
        size -= mapsize;
    }
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
    case 3:
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, mem_reg_prop[0],
                                     2, mem_reg_prop[1],
                                     2, mem_reg_prop[2],
                                     2, mem_reg_prop[3],
                                     2, mem_reg_prop[4],
                                     2, mem_reg_prop[5]);
        break;
    case 4:
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, mem_reg_prop[0],
                                     2, mem_reg_prop[1],
                                     2, mem_reg_prop[2],
                                     2, mem_reg_prop[3],
                                     2, mem_reg_prop[4],
                                     2, mem_reg_prop[5],
                                     2, mem_reg_prop[6],
                                     2, mem_reg_prop[7]);
        break;
    default:
        g_assert_not_reached();
    }
    g_free(name);
}

static void sigi_virt_modify_dtb(const struct arm_boot_info *binfo,
                                    void *fdt)
{
    HobotSigiVirt *s = container_of(binfo, HobotSigiVirt, binfo);

    fdt_add_memory_nodes(s, fdt, binfo->ram_size);
}

static void *sigi_virt_get_dtb(const struct arm_boot_info *binfo,
                                  int *fdt_size)
{
    const HobotSigiVirt *board = container_of(binfo, HobotSigiVirt, binfo);

    *fdt_size = board->fdt_size;
    return board->fdt;
}

static void sigi_virt_machine_done(Notifier *notifier, void *data)
{
    HobotSigiVirt *s = container_of(notifier, HobotSigiVirt,
                                    machine_done);
    MachineState *ms = MACHINE(s);
    ARMCPU *cpu = ARM_CPU(first_cpu);
    struct arm_boot_info *info = &s->binfo;
    AddressSpace *as = arm_boot_address_space(cpu, info);

    if (arm_load_dtb(info->dtb_start, info, info->dtb_limit, as, ms) < 0) {
        exit(1);
    }
}

static void sd_plugin_card(CadenceSDHCIState *cdns, DriveInfo *di)
{
    BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;
    DeviceState *card;

    card = qdev_new(TYPE_SD_CARD);
    object_property_add_child(OBJECT(cdns), "card[*]", OBJECT(card));
    qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
    qdev_realize_and_unref(card, cdns->bus, &error_fatal);
}

static void sigi_virt_init(MachineState *machine)
{
    HobotSigiVirt *s = HOBOT_SIGI_VIRT_MACHINE(machine);
    int psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;

    /*
     * If the user provides an Operating System to be loaded, we expect them
     * to use the -kernel command line option.
     *
     * Users can load firmware or boot-loaders with the -device loader options.
     *
     * When loading an OS, we generate a dtb and let arm_load_kernel() select
     * where it gets loaded. This dtb will be passed to the kernel in x0.
     *
     * If there's no -kernel option, we generate a DTB and place it at 0x1000
     * for the bootloaders or firmware to pick up.
     *
     * If users want to provide their own DTB, they can use the -dtb option.
     * These dtb's will have their memory nodes modified to match QEMU's
     * selected ram_size option before they get passed to the kernel or fw.
     *
     * When loading an OS, we turn on QEMU's PSCI implementation with SMC
     * as the PSCI conduit. When there's no -kernel, we assume the user
     * provides EL3 firmware to handle PSCI.
     *
     * Even if the user provides a kernel filename, arm_load_kernel()
     * may suppress PSCI if it's going to boot that guest code at EL3.
     */
    if (machine->kernel_filename) {
        psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    }

    object_initialize_child(OBJECT(machine), "sigi-virt", &s->soc,
                            TYPE_SIGI_SOC);
    object_property_set_link(OBJECT(&s->soc), "ddr", OBJECT(machine->ram),
                             &error_abort);
    object_property_set_bool(OBJECT(&s->soc), "has-emmc", s->cfg.has_emmc,
                             &error_abort);

    if (!machine->kernel_filename) {
        object_property_set_bool(OBJECT(&s->soc), "secure", false, NULL);
        object_property_set_bool(OBJECT(&s->soc), "virtualization", false, NULL);
    } else {
        object_property_set_bool(OBJECT(&s->soc), "secure", true, NULL);
        object_property_set_bool(OBJECT(&s->soc), "virtualization", true, NULL);
    }
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    fdt_create(s);
    fdt_add_uart_nodes(s);
    fdt_add_sdhci_nodes(s);
    fdt_add_gic_nodes(s);
    fdt_add_timer_nodes(s);
    fdt_add_cpu_nodes(s, psci_conduit);
    fdt_add_clk_node(s, "/clk125", 125000000, s->phandle.clk_125Mhz);
    fdt_add_clk_node(s, "/clk25", 25000000, s->phandle.clk_25Mhz);
    fdt_add_clk_node(s, "/clk200", 200000000, s->phandle.clk_200Mhz);

    if (!s->cfg.has_emmc) {
        sd_plugin_card(&s->soc.cpu_subsys.peri.mmc[0],
            drive_get(IF_SD, 0, 0));
    }
    /* Plugin SD cards.  */
    sd_plugin_card(&s->soc.cpu_subsys.peri.mmc[1],
            drive_get(IF_SD, 0, s->cfg.has_emmc? 0 : 1));

    s->binfo.ram_size = machine->ram_size;
    s->binfo.loader_start = MM_TOP_DDR;
    s->binfo.get_dtb = sigi_virt_get_dtb;
    s->binfo.modify_dtb = sigi_virt_modify_dtb;
    s->binfo.psci_conduit = psci_conduit;
    if (!machine->kernel_filename) {
        s->binfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
        s->binfo.dtb_limit = 0x10000;
        s->binfo.skip_dtb_autoload = true;
    }
    arm_load_kernel(&s->soc.cpu_subsys.apu.cpu[0], machine, &s->binfo);

     if (!machine->kernel_filename) {
        s->machine_done.notify = sigi_virt_machine_done;
        qemu_add_machine_init_done_notifier(&s->machine_done);
     }
}

static void sigi_virt_machine_instance_init(Object *obj)
{
}

static void sigi_virt_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hobot Sigi Virtual Development Board";
    mc->init = sigi_virt_init;
    mc->min_cpus = SIGI_SOC_NR_ACPUS + SIGI_SOC_NR_RCPUS;
    mc->max_cpus = SIGI_SOC_NR_ACPUS + SIGI_SOC_NR_RCPUS;
    mc->default_cpus = SIGI_SOC_NR_ACPUS + SIGI_SOC_NR_RCPUS;
    mc->no_cdrom = true;
    mc->default_ram_id = "ddr";
    mc->no_sdcard = 1;      // disable default_sdcard
    object_class_property_add_bool(oc, "emmc", NULL,
		    sigi_virt_set_emmc);
}

static const TypeInfo sigi_virt_machine_init_typeinfo = {
    .name       = TYPE_HOBOT_SIGI_VIRT_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = sigi_virt_machine_class_init,
    .instance_init = sigi_virt_machine_instance_init,
    .instance_size = sizeof(HobotSigiVirt),
};

static void sigi_virt_machine_init_register_types(void)
{
    type_register_static(&sigi_virt_machine_init_typeinfo);
}

type_init(sigi_virt_machine_init_register_types)
